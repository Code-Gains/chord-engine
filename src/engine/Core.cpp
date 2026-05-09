#include <chrono>
using Clock = std::chrono::high_resolution_clock;
#include <cmath>
#include "Core.h"
//#include "VkInit.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include "Transform.h"
#include "Camera.h"
#include "CameraSystem.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_descriptors.h"
#pragma clang diagnostic pop

// ECS PORT
#include "EcsDebugger.h"

namespace Engine {
#ifndef NDEBUG
    constexpr bool gUseValidationLayers = true; // TODO move to global settings later
#else
    constexpr bool gUseValidationLayers = false; // TODO move to global settings later
#endif
    // TODO Disable on NON Debug Builds

    ThreadPool Core::_threadPool{std::thread::hardware_concurrency()};

    void Core::InitWindow() {
        _window = std::make_unique<WindowGLFW>(1280, 720, "Chord Game Engine");
    }

    void Core::InitVulkan() {
        vkb::InstanceBuilder builder;
        // make the vulkan instance, with basic debug features
	    auto inst_ret = builder.set_app_name("Example Vulkan Application")
            .request_validation_layers(gUseValidationLayers)
            .use_default_debug_messenger()
            .require_api_version(1, 3, 0)
            .build();
        vkb::Instance vkb_inst = inst_ret.value();

	    // grab the instance 
	    _instance = vkb_inst.instance;
	    _debugMessenger = vkb_inst.debug_messenger;

        // GLFW Vulkan Surface
        _surface = _window->CreateAndGetWindowSurface(_instance);

        //vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features features{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        features.dynamicRendering = true;
        features.synchronization2 = true;

        //vulkan 1.2 features
        VkPhysicalDeviceVulkan12Features features12{ .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        features12.bufferDeviceAddress = true;
        features12.descriptorIndexing = true;


        //use vkbootstrap to select a gpu. 
        //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
        vkb::PhysicalDeviceSelector selector{ vkb_inst };
        vkb::PhysicalDevice physicalDevice = selector
            .set_minimum_version(1, 3)
            .set_required_features_13(features)
            .set_required_features_12(features12)
            .set_surface(_surface)
            .select()
            .value();


        //create the final vulkan device
        vkb::DeviceBuilder deviceBuilder{ physicalDevice };

        vkb::Device vkbDevice = deviceBuilder.build().value();

        // Get the VkDevice handle used in the rest of a vulkan application
        _device = vkbDevice.device;
        _chosenGPU = physicalDevice.physical_device;

        // use vkbootstrap to get a Graphics queue
        _graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
        _graphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

         // initialize the memory allocator
        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = _chosenGPU;
        allocatorInfo.device = _device;
        allocatorInfo.instance = _instance;
        allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        vmaCreateAllocator(&allocatorInfo, &_allocator);

        _mainDeletionQueue.push_function([&]() {
            vmaDestroyAllocator(_allocator);
        });
    }

    void Core::InitQueries()
    {
        VkQueryPoolCreateInfo qp{};
        qp.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        qp.queryType = VK_QUERY_TYPE_TIMESTAMP;
        qp.queryCount = 2;

        for (int i = 0; i < FRAME_OVERLAP; i++) {
            VK_CHECK(vkCreateQueryPool(_device, &qp, nullptr, &_frames[i]._gpuQueryPool));
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(_chosenGPU, &props);
        _timestampPeriod = props.limits.timestampPeriod;

        _mainDeletionQueue.push_function([this]() {
            for (int i = 0; i < FRAME_OVERLAP; i++) {
                vkDestroyQueryPool(_device, _frames[i]._gpuQueryPool, nullptr);
            }
        });
    }

    void Core::InitSwapchain() {
        CreateSwapchain(_window->GetWidth(), _window->GetHeight());
        CreateDrawImages(_window->GetWidth(), _window->GetHeight());

    }

    void Core::InitCommands() {
        //create a command pool for commands submitted to the graphics queue.
	    //we also want the pool to allow for resetting of individual command buffers
        VkCommandPoolCreateInfo commandPoolInfo = vkinit::command_pool_create_info(_graphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

        for (int i = 0; i < FRAME_OVERLAP; i++) {

            VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

            // allocate the default command buffer that we will use for rendering
            VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_frames[i]._commandPool, 1);

            VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_frames[i]._mainCommandBuffer));
        }

        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

        // allocate the command buffer for immediate submits
        VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_immCommandPool, 1);

        VK_CHECK(vkAllocateCommandBuffers(_device, &cmdAllocInfo, &_immCommandBuffer));

        _mainDeletionQueue.push_function([this]() { 
            vkDestroyCommandPool(_device, _immCommandPool, nullptr);
        });
    }

    void Core::InitSyncStructures() {
        //create syncronization structures
        //one fence to control when the gpu has finished rendering the frame,
        //and 2 semaphores to syncronize rendering with swapchain
        //we want the fence to start signalled so we can wait on it on the first frame
        VkFenceCreateInfo fenceCreateInfo = vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
        VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
        _imageAvailableSemaphores.resize(FRAME_OVERLAP);
        for (int i = 0; i < FRAME_OVERLAP; i++) {
            VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
            VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_imageAvailableSemaphores[i]));
        }

        // per swapchain image sync
        _renderFinishedSemaphores.resize(_swapchainImages.size());
        for (int i = 0; i < _swapchainImages.size(); i++) {
            VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_renderFinishedSemaphores[i]));
        }

        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));
        _mainDeletionQueue.push_function([this]() { 
            vkDestroyFence(_device, _immFence, nullptr); 
        });
    }

    void Core::CreateSwapchain(uint32_t width, uint32_t height) {
        vkb::SwapchainBuilder swapchainBuilder{ _chosenGPU,_device,_surface };

        _swapchainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

        vkb::Swapchain vkbSwapchain = swapchainBuilder
            //.use_default_format_selection()
            .set_desired_format(VkSurfaceFormatKHR{ .format = _swapchainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
            //use vsync present mode
            .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
            .set_desired_extent(width, height)
            .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
            .build()
            .value();

        _swapchainExtent = vkbSwapchain.extent;
        // store swapchain and its related images
        _swapchain = vkbSwapchain.swapchain;
        _swapchainImages = vkbSwapchain.get_images().value();
        _swapchainImageViews = vkbSwapchain.get_image_views().value();
    }

     void Core::RecreateSwapchain(uint32_t width, uint32_t height) {
        // Destroy current swapchain
        vkDeviceWaitIdle(_device);

        // Cleanup resources tied to current swapchain
        
        CleanupDrawImages();
        CleanupSwapchainResources();

        // Create new swapchain
        CreateSwapchain(width, height);
        CreateDrawImages(width, height);
        UpdateDrawImageDescriptor();
        _window->ResetResizedFlag();
    }

    void Core::CleanupSwapchainResources() {
        for (auto imageView : _swapchainImageViews) {
            vkDestroyImageView(_device, imageView, nullptr);
        }
        _swapchainImageViews.clear();

        if (_swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(_device, _swapchain, nullptr);
            _swapchain = VK_NULL_HANDLE;
        }
    }

    void Core::DestroySwapchain() {
        vkDestroySwapchainKHR(_device, _swapchain, nullptr);

        // destroy swapchain resources
        for (int i = 0; i < _swapchainImageViews.size(); i++) {
            vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
        }
    }

    void Core::UpdateDrawImageDescriptor()
    {
        if (_drawImage.imageView == VK_NULL_HANDLE || _drawImageDescriptors == VK_NULL_HANDLE)
            return; // safe guard if called too early

        DescriptorWriter writer;
        writer.write_image(0, _drawImage.imageView, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        writer.update_set(_device,_drawImageDescriptors);
    }

    void Core::CleanupDrawImageDescriptors()
    {
        globalDescriptorAllocator.DestroyPool(_device);
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _gpuSceneDataDescriptorLayout , nullptr);
        vkDestroyDescriptorSetLayout(_device, _singleImageDescriptorLayout , nullptr);
    }

    void Core::CreateDrawImages(uint32_t width, uint32_t height)
    {
        VkExtent3D drawImageExtent = { width, height, 1 };

        // // --------------------------
        // // MULTISAMPLED COLOR IMAGE
        // // --------------------------
        // _msaaColorImage = CreateImage(
        //     drawImageExtent,
        //     VK_FORMAT_R16G16B16A16_SFLOAT,
        //     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        //     VK_IMAGE_USAGE_STORAGE_BIT |
        //     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
        //     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        //     false,       // no mipmaps
        //     VK_SAMPLE_COUNT_4_BIT
        // );

        // // --------------------------
        // // MULTISAMPLED DEPTH IMAGE
        // // --------------------------
        // _msaaDepthImage = CreateImage(
        //     drawImageExtent,
        //     VK_FORMAT_D32_SFLOAT,
        //     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        //     false,
        //     VK_SAMPLE_COUNT_4_BIT
        // );

        // --------------------------
        // SINGLE-SAMPLE COLOR IMAGE (resolve target)
        // --------------------------
        _drawImage = CreateImage(
            drawImageExtent,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            false,
            VK_SAMPLE_COUNT_1_BIT
        );

        // --------------------------
        // SINGLE-SAMPLE DEPTH IMAGE (optional fallback)
        // --------------------------
        _depthImage = CreateImage(
            drawImageExtent,
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
            false,
            VK_SAMPLE_COUNT_1_BIT
        );
    }

    void Core::CleanupDrawImages()
    {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);

        // vkDestroyImageView(_device, _msaaColorImage.imageView, nullptr);
        // vmaDestroyImage(_allocator, _msaaColorImage.image, _msaaColorImage.allocation);

        // vkDestroyImageView(_device, _msaaDepthImage.imageView, nullptr);
        // vmaDestroyImage(_allocator, _msaaDepthImage.image, _msaaDepthImage.allocation);
    }

    void Core::InitPipelines()
    {
        InitBackgroundPipelines();
        InitTrianglePipeline();

        size_t maxInstances = 1000000; // upper bound
        _instanceBuffer = CreateBuffer(
            sizeof(InstanceData) * maxInstances,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        InitMeshPipeline();
        InitInstancedMeshPipeline();
    }

    void Core::InitBackgroundPipelines()
    {
        VkPipelineLayoutCreateInfo computeLayout{};
        computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        computeLayout.pNext = nullptr;
        computeLayout.pSetLayouts = &_drawImageDescriptorLayout;
        computeLayout.setLayoutCount = 1;

        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(ComputePushConstants) ;
        pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        computeLayout.pPushConstantRanges = &pushConstant;
        computeLayout.pushConstantRangeCount = 1;

        VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

        VkShaderModule gradientShader;
        // if (!vkutil::load_shader_module("../../../shaders/gradient_color.comp.spv", _device, &gradientShader)) {
        //     ENGINE_LOG_ERROR("Error when building the compute shader");
        // }
        if (!vkutil::load_shader_module("../../../shaders/gradient.comp.spv", _device, &gradientShader)) {
            ENGINE_LOG_ERROR("Error when building the compute shader");
        }


        VkShaderModule skyShader;
        if (!vkutil::load_shader_module("../../../shaders/sky.comp.spv", _device, &skyShader)) {
            ENGINE_LOG_ERROR("Error when building the compute shader");
        }

        VkPipelineShaderStageCreateInfo stageinfo{};
        stageinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageinfo.pNext = nullptr;
        stageinfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stageinfo.module = gradientShader;
        stageinfo.pName = "main";

        VkComputePipelineCreateInfo computePipelineCreateInfo{};
        computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        computePipelineCreateInfo.pNext = nullptr;
        computePipelineCreateInfo.layout = _gradientPipelineLayout;
        computePipelineCreateInfo.stage = stageinfo;

        ComputeEffect gradient;
        gradient.layout = _gradientPipelineLayout;
        gradient.name = "gradient";
        gradient.data = {};

        //default colors
        gradient.data.data1 = glm::vec4(1, 0, 0, 1);
        gradient.data.data2 = glm::vec4(0, 0, 1, 1);
                
        VK_CHECK(vkCreateComputePipelines(_device,VK_NULL_HANDLE,1,&computePipelineCreateInfo, nullptr, &gradient.pipeline));

        //change the shader module only to create the sky shader
        computePipelineCreateInfo.stage.module = skyShader;

        ComputeEffect sky;
        sky.layout = _gradientPipelineLayout;
        sky.name = "sky";
        sky.data = {};
        //default sky parameters
        sky.data.data1 = glm::vec4(0.1, 0.2, 0.4 ,0.97);

        VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

        _backgroundEffects.push_back(gradient);
        _backgroundEffects.push_back(sky);


        vkDestroyShaderModule(_device, gradientShader, nullptr);
        vkDestroyShaderModule(_device, skyShader, nullptr);
        
        VkPipeline gradientPipeline = gradient.pipeline;
        VkPipeline skyPipeline = sky.pipeline;
        VkPipelineLayout layout = _gradientPipelineLayout;

        _mainDeletionQueue.push_function([this, gradientPipeline, skyPipeline, layout]() {
            vkDestroyPipeline(_device, gradientPipeline, nullptr);
            vkDestroyPipeline(_device, skyPipeline, nullptr);
            vkDestroyPipelineLayout(_device, layout, nullptr);
        });
    }

    void Core::InitDescriptors()
    {
        // //create a descriptor pool that will hold 10 sets with 1 image each
        // std::vector<DescriptorAllocator::PoolSizeRatio> sizes =
        // {
        //     { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
        // };

        // globalDescriptorAllocator.InitPool(_device, 10, sizes);

        // //make the descriptor set layout for our compute draw
        // {
        //     DescriptorLayoutBuilder builder;
        //     builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        //     _drawImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
        // }

        // //allocate a descriptor set for our draw image
        // _drawImageDescriptors = globalDescriptorAllocator.Allocate(_device,_drawImageDescriptorLayout);	

        // VkDescriptorImageInfo imgInfo{};
        // imgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        // imgInfo.imageView = _drawImage.imageView;
        
        // VkWriteDescriptorSet drawImageWrite = {};
        // drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        // drawImageWrite.pNext = nullptr;
        
        // drawImageWrite.dstBinding = 0;
        // drawImageWrite.dstSet = _drawImageDescriptors;
        // drawImageWrite.descriptorCount = 1;
        // drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        // drawImageWrite.pImageInfo = &imgInfo;

        // vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

        // 1. Create the descriptor pool (keep it alive for runtime)
        std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 }
        };
        globalDescriptorAllocator.InitPool(_device, 10, sizes);

        // 2. Create the descriptor set layout
        {
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            _drawImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
        }

        // + add gpu scene data descriptor layout
        {
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
            _gpuSceneDataDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        }

        // +
        {
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            _singleImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
        }

        // 3. Allocate the descriptor set
        _drawImageDescriptors = globalDescriptorAllocator.Allocate(_device, _drawImageDescriptorLayout);


        for (int i = 0; i < FRAME_OVERLAP; i++) {
            // create a descriptor pool
            std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = { 
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
            };

            _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
            _frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
        
            _mainDeletionQueue.push_function([&, i]() {
                _frames[i]._frameDescriptors.destroy_pools(_device);
            });
        }

        // 4. Update descriptor set to point to the current draw image
        UpdateDrawImageDescriptor();

    

        //make sure both the descriptor allocator and the new layout get cleaned up properly
        // _mainDeletionQueue.push_function([&]() {
        //     globalDescriptorAllocator.DestroyPool(_device);
        //     vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        // });
    }

    AllocatedBuffer Core::CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    {
        // allocate buffer
        VkBufferCreateInfo bufferInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferInfo.pNext = nullptr;
        bufferInfo.size = allocSize;

        bufferInfo.usage = usage;

        VmaAllocationCreateInfo vmaallocInfo = {};
        vmaallocInfo.usage = memoryUsage;
        vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        AllocatedBuffer newBuffer;

        // allocate the buffer
        VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation,
            &newBuffer.info));

        // Get device address if requested
        if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
            VkBufferDeviceAddressInfo addressInfo{};
            addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
            addressInfo.buffer = newBuffer.buffer;
            newBuffer.deviceAddress = vkGetBufferDeviceAddress(_device, &addressInfo);
        } else {
            newBuffer.deviceAddress = 0; // not used
        }

        return newBuffer;
    }

    void Core::DestroyBuffer(const AllocatedBuffer &buffer)
    {
        vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
    }

    GPUMeshBuffers Core::UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
    {
        const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
        const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

        GPUMeshBuffers newSurface;

        //create vertex buffer
        newSurface.vertexBuffer = CreateBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

        //find the adress of the vertex buffer
        VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = newSurface.vertexBuffer.buffer };
        newSurface.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAdressInfo);

        //create index buffer
        newSurface.indexBuffer = CreateBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY);

        AllocatedBuffer staging = CreateBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

        void* data = staging.allocation->GetMappedData();

        // copy vertex buffer
        memcpy(data, vertices.data(), vertexBufferSize);
        // copy index buffer
        memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

        ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkBufferCopy vertexCopy{ 0 };
            vertexCopy.dstOffset = 0;
            vertexCopy.srcOffset = 0;
            vertexCopy.size = vertexBufferSize;

            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.vertexBuffer.buffer, 1, &vertexCopy);

            VkBufferCopy indexCopy{ 0 };
            indexCopy.dstOffset = 0;
            indexCopy.srcOffset = vertexBufferSize;
            indexCopy.size = indexBufferSize;

            vkCmdCopyBuffer(cmd, staging.buffer, newSurface.indexBuffer.buffer, 1, &indexCopy);
        });

        DestroyBuffer(staging);

        return newSurface;
    }

    AllocatedImage Core::CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped, VkSampleCountFlagBits samples)
    {
        AllocatedImage newImage;
        newImage.imageFormat = format;
        newImage.imageExtent = size;

        VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
        if (mipmapped) {
            img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
        }

        // always allocate images on dedicated GPU memory
        VmaAllocationCreateInfo allocinfo = {};
        allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // allocate and create the image
        VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

        // if the format is a depth format, we will need to have it use the correct
        // aspect flag
        VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
        if (format == VK_FORMAT_D32_SFLOAT) {
            aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        // build a image-view for the image
        VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
        view_info.subresourceRange.levelCount = img_info.mipLevels;

        VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

        return newImage;
    }

    AllocatedImage Core::CreateImage(
        void* data,
        VkExtent3D size,
        VkFormat format,
        VkImageUsageFlags usage,
        bool mipmapped,
        VkSampleCountFlagBits samples) // <-- added
    {
        AllocatedImage newImage;
        newImage.imageFormat = format;
        newImage.imageExtent = size;

        VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
        img_info.samples = samples; // <-- use MSAA sample count

        if (mipmapped) {
            img_info.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(size.width, size.height)))) + 1;
        }

        // always allocate images on dedicated GPU memory
        VmaAllocationCreateInfo allocinfo = {};
        allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        // allocate and create the image
        VK_CHECK(vmaCreateImage(_allocator, &img_info, &allocinfo, &newImage.image, &newImage.allocation, nullptr));

        // select correct aspect
        VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
        if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
            aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        // build an image-view
        VkImageViewCreateInfo view_info = vkinit::imageview_create_info(format, newImage.image, aspectFlag);
        view_info.subresourceRange.levelCount = img_info.mipLevels;

        VK_CHECK(vkCreateImageView(_device, &view_info, nullptr, &newImage.imageView));

        return newImage;
    }

    void Core::DestroyImage(const AllocatedImage &img)
    {
        vkDestroyImageView(_device, img.imageView, nullptr);
        vmaDestroyImage(_allocator, img.image, img.allocation);
    }

    void Core::Draw()
    {
        // auto registryView = _registry.view<InputState>();

        // for (auto entity : registryView) {
        //     auto& inputState = registryView.get<InputState>(entity);
            //_inputSystem.Update(inputState, _window->GetNativeHandle());
        //}
        // for (auto& system : _systems) {
        //     system->Update(0.0f);
        // }
        auto registryView = _registry.view<Camera>();

        auto cameraEntity = *registryView.begin();
        auto& camera = registryView.get<Camera>(cameraEntity);

        FrameData& frameData = GetCurrentFrame();
        //wait until the gpu has finished rendering the last frame. Timeout of 1 second
        VK_CHECK(vkWaitForFences(_device, 1, &GetCurrentFrame()._renderFence, true, 1000000000));

        // After vkWaitForFences, before anything else:
        if (_pendingScreenshot.buffer != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(_device);
            void* data;
            vmaMapMemory(_allocator, _pendingScreenshot.allocation, &data);


            uint16_t* src = (uint16_t*)data;
            uint32_t w = _pendingScreenshotExtent.width;
            uint32_t h = _pendingScreenshotExtent.height;

            // Convert R16G16B16A16_SFLOAT -> R8G8B8A8
            std::vector<uint8_t> pixels(w * h * 4);
            for (uint32_t i = 0; i < w * h * 4; i++)
            {
                // Proper float16 -> float32 conversion
                uint16_t h16 = src[i];
                uint32_t sign     = (h16 >> 15) & 0x1;
                uint32_t exponent = (h16 >> 10) & 0x1F;
                uint32_t mantissa = h16 & 0x3FF;
                

                float f;
                if (exponent == 0) {
                    f = std::ldexp((float)mantissa, -24); // denormal
                } else if (exponent == 31) {
                    f = 0.0f; // treat Inf and NaN as 0 instead of garbage
                } else {
                    f = std::ldexp((float)(mantissa | 0x400), (int)exponent - 25);
                }

                if (sign) f = -f;

                // Guard against any remaining NaN/Inf slipping through
                if (!std::isfinite(f)) f = 0.0f;

                pixels[i] = (uint8_t)(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
            }

            stbi_write_png("screenshot.png", w, h, 4, pixels.data(), w * 4);

            vmaUnmapMemory(_allocator, _pendingScreenshot.allocation);
            DestroyBuffer(_pendingScreenshot);
            _pendingScreenshot.buffer = VK_NULL_HANDLE;
        }

        frameData._deletionQueue.flush();
        frameData._frameDescriptors.clear_pools(_device);
        VK_CHECK(vkResetFences(_device, 1, &frameData._renderFence));

        // // pick per frame semaphore
        VkSemaphore imageAvailableSemaphore = _imageAvailableSemaphores[_frameNumber % FRAME_OVERLAP];
	    uint32_t swapchainImageIndex;
        VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
            imageAvailableSemaphore, nullptr, &swapchainImageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain(_window->GetWidth(), _window->GetHeight());
            result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
                imageAvailableSemaphore, nullptr, &swapchainImageIndex);
            if (result != VK_SUCCESS) {
                throw std::runtime_error("Failed to recreate swapchain image on VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR!");
            }
        }

        // -----------------------------------------------------------------------
        VkCommandBuffer cmd = GetCurrentFrame()._mainCommandBuffer;
        // begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
        VkCommandBufferBeginInfo cmdBeginInfo =
            vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        // start the command buffer recording
        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        _drawExtent.width = _drawImage.imageExtent.width;
        _drawExtent.height = _drawImage.imageExtent.height;

        // transition our main draw image into general layout so we can write into it
        // we will overwrite it all so we dont care about what was the older layout
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        DrawBackground(cmd);
        
        //vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
        DrawGeometry(cmd);
        {
            if (camera.screenshotRequested)
            {
                 camera.screenshotRequested = false;

                // 1. Transition draw image to transfer source
                vkutil::transition_image(cmd, _drawImage.image,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

                // Ensure all color attachment writes are visible before the transfer read
                VkMemoryBarrier2 memBarrier = {};
                memBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

                VkDependencyInfo depInfo = {};
                depInfo.sType                = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                depInfo.memoryBarrierCount   = 1;
                depInfo.pMemoryBarriers      = &memBarrier;

                vkCmdPipelineBarrier2(cmd, &depInfo);

                // Query actual row pitch
                VkImageSubresource subResource = {};
                subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                subResource.mipLevel = 0;
                subResource.arrayLayer = 0;
                VkSubresourceLayout subResourceLayout;
                vkGetImageSubresourceLayout(_device, _drawImage.image, &subResource, &subResourceLayout);
                
                // 2. Create a host-visible staging buffer
                VkDeviceSize imageSize = _drawExtent.width * _drawExtent.height * 8; // RGBA16
                AllocatedBuffer stagingBuffer = CreateBuffer(imageSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY);

                // 3. Copy image to buffer
                VkBufferImageCopy copyRegion = {};
                copyRegion.bufferOffset = 0;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;
                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = 0;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;
                copyRegion.imageExtent = { _drawExtent.width, _drawExtent.height, 1 };

                vkCmdCopyImageToBuffer(cmd, _drawImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    stagingBuffer.buffer, 1, &copyRegion);

                // 4. Transition back so the rest of the frame continues normally
                vkutil::transition_image(cmd, _drawImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

                // 5. After command buffer submission + fence wait, map and save
                //    Queue this as a post-frame callback, or do it inline after vkWaitForFences next frame.
                //    Simplest: store the buffer and save after the fence signals.
                _pendingScreenshot = stagingBuffer; // store for readback
                _pendingScreenshotExtent = _drawExtent;
            }
           // record png image
        }

        //transition the draw image and the swapchain image into their correct transfer layouts
        vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        //vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkutil::transition_image(cmd,_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        // execute a copy from the draw image into the swapchain
        vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

        // set swapchain image layout to Attachment Optimal so we can draw it
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        //draw imgui into the swapchain image
        DrawImGui(cmd,  _swapchainImageViews[swapchainImageIndex]);

        // set swapchain image layout to Present so we can draw it
        vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        //finalize the command buffer (we can no longer add commands, but it can now be executed)
        VK_CHECK(vkEndCommandBuffer(cmd));
        // ---------------------------------------------------------------------------------

        // prepare the submission to the queue. 
        // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
        // we will signal the _renderSemaphore, to signal that rendering has finished

        VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);	
        
        VkSemaphore renderFinishedSemaphore = _renderFinishedSemaphores[swapchainImageIndex];
        VkSemaphoreSubmitInfo waitInfo =
            vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
            imageAvailableSemaphore);
        VkSemaphoreSubmitInfo signalInfo =
            vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
            renderFinishedSemaphore); // always synchronize swapchain image with
                                                             // render semaphore	
        
        VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);	
        // submit command buffer to the queue and execute it.
        // _renderFence will now block until the graphic commands finish execution
        VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, GetCurrentFrame()._renderFence));

        // prepare present
        // this will put the image we just rendered to into the visible window.
        // we want to wait on the _renderSemaphore for that, 
        // as its necessary that drawing commands have finished before the image is displayed to the user
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.swapchainCount = 1;
        //presentInfo.pNext = nullptr;
        presentInfo.pSwapchains = &_swapchain;
        presentInfo.pImageIndices = &swapchainImageIndex;
        presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
        presentInfo.waitSemaphoreCount = 1;

        //VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
        VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            int width = 0, height = 0;
            glfwGetFramebufferSize(_window->GetNativeHandle(), &width, &height);
            glfwWaitEvents();
            RecreateSwapchain(width, height);
        }
        else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("Failed to present swapchain image!");
        }
        // increase the number of frames drawn
        _frameNumber++;
    }

    void Core::DrawUi()
    {
        //_ecsDebugger.Draw();

		// if (ImGui::Begin("Background")) {

        //     ComputeEffect& selected = _backgroundEffects[_currentBackgroundEffect];

        //     // Header
        //     ImGui::TextDisabled("Active Effect");
        //     ImGui::SameLine();
        //     ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1.0f), "%s", selected.name);

        //     ImGui::Separator();

        //     // Effect selection
        //     ImGui::PushItemWidth(-1);
        //     ImGui::SliderInt(
        //         "##EffectIndex",
        //         &_currentBackgroundEffect,
        //         0,
        //         (int)_backgroundEffects.size() - 1
        //     );
        //     ImGui::PopItemWidth();

        //     ImGui::Spacing();

        //     // Parameters section
        //     ImGui::TextDisabled("Parameters");
        //     ImGui::Indent();

        //     ImGui::DragFloat4("Data 1", &selected.data.data1.x, 0.01f);
        //     ImGui::DragFloat4("Data 2", &selected.data.data2.x, 0.01f);
        //     ImGui::DragFloat4("Data 3", &selected.data.data3.x, 0.01f);
        //     ImGui::DragFloat4("Data 4", &selected.data.data4.x, 0.01f);

        //     ImGui::Unindent();
        // }
        // ImGui::End();

    }

    void Core::DrawBackground(VkCommandBuffer cmd)
    {

        // //make a clear-color from frame number. This will flash with a 120 frame period.
        VkClearColorValue clearValue;
        auto registryView = _registry.view<Camera>();

        if (!registryView.empty()) {
            auto cameraEntity = *registryView.begin();
            auto& camera = registryView.get<Camera>(cameraEntity);

            clearValue.float32[0] = camera.clearColor.r;
            clearValue.float32[1] = camera.clearColor.g;
            clearValue.float32[2] = camera.clearColor.b;
            clearValue.float32[3] = camera.clearColor.a;
        }

        VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
        
        // //clear image
        vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
        //bind the gradient drawing compute pipeline


        //vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);

        // // bind the descriptor set containing the draw image for the compute pipeline
        // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

        // // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
        // vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);

        // bind the gradient drawing compute pipeline

        // ComputeEffect& effect = _backgroundEffects[1];
        // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
        // //vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipeline);

        // // bind the descriptor set containing the draw image for the compute pipeline
        // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

        // ComputePushConstants pc;

        // pc.data1 = glm::vec4(1, 0, 0, 1);
        // pc.data2 = glm::vec4(0, 0, 1, 1);

        // vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &pc);
        // // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
        // vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);

        // ComputeEffect& effect = _backgroundEffects[_currentBackgroundEffect];

        // // bind the background compute pipeline
        // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

        // // bind the descriptor set containing the draw image for the compute pipeline
        // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);
        // if (_currentBackgroundEffect == 0)
        // {
        //     effect.data.data1.x = _frameNumber / 100.0f;
        // }

        // vkCmdPushConstants(cmd, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);
        // // execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
        // vkCmdDispatch(cmd, std::ceil(_drawExtent.width / 16.0), std::ceil(_drawExtent.height / 16.0), 1);
    }

    void Core::DrawGeometry(VkCommandBuffer cmd)
    {
        glm::mat4 viewMatrix;;
        glm::mat4 projectionMatrix;
        auto registryView = _registry.view<Camera, Transform>();

        for (auto entity : registryView) {
            auto& camera = registryView.get<Camera>(entity);
            auto& transform = registryView.get<Transform>(entity);

            viewMatrix = camera.GetViewMatrix(transform);
            projectionMatrix = camera.GetProjectionMatrix();
        }

        static float time = 0;
        time += 0.00001;
        
        static bool firstMouse = true;
        static double lastX = 0, lastY = 0;
        double mouseX, mouseY;
        glfwGetCursorPos(_window->GetNativeHandle(), &mouseX, &mouseY);
        //UpdateCamera(camera, _window->GetNativeHandle(), time);
        //UpdateCameraRotation(camera, mouseX, mouseY, lastX, lastY, firstMouse);

        //allocate a new uniform buffer for the scene data
        AllocatedBuffer gpuSceneDataBuffer = CreateBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        //add it to the deletion queue of this frame so it gets deleted once its been used
        GetCurrentFrame()._deletionQueue.push_function([=, this]() {
            DestroyBuffer(gpuSceneDataBuffer);
        });

        //write the buffer
        GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
        *sceneUniformData = sceneData;

        //create a descriptor set that binds that buffer and update it
        VkDescriptorSet globalDescriptor = GetCurrentFrame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);

        DescriptorWriter writer;
        writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.update_set(_device, globalDescriptor);



        //begin a render pass  connected to our draw image
        VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingAttachmentInfo depthAttachment = vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

        VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
        
        vkCmdBeginRendering(cmd, &renderInfo);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

        //set dynamic viewport and scissor
        VkViewport viewport = {};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = _drawExtent.width;
        viewport.height = _drawExtent.height;
        viewport.minDepth = 0.f;
        viewport.maxDepth = 1.f;

        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset.x = 0;
        scissor.offset.y = 0;
        scissor.extent.width = _drawExtent.width;
        scissor.extent.height = _drawExtent.height;

        vkCmdSetScissor(cmd, 0, 1, &scissor);

        //launch a draw command to draw 3 vertices
        // vkCmdDraw(cmd, 3, 1, 0, 0);

        // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

        //bind a texture
        VkDescriptorSet imageSet = GetCurrentFrame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
        {
            DescriptorWriter writer;
            writer.write_image(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            writer.update_set(_device, imageSet);
        }

        // vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);

        //GPUDrawPushConstants push_constants;
        // push_constants.worldMatrix = glm::mat4{ 1.f };
        // push_constants.vertexBuffer = rectangle.vertexBufferAddress;

        // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        // vkCmdBindIndexBuffer(cmd, rectangle.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        //vkCmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
        
        //pos2 = glm::rotate(pos2, glm::vec3{0, 0, 0});


        // // invert the Y direction on projection matrix so that we are more similar
        // // to opengl and gltf axis

        //     glm::mat4 projectionx(
        //     1.0,  0.0,  0.0,  0.0,
        //     0.0, -1.0,  0.0,  0.0,
        //     0.0,  0.0,  0.5,  0.0,
        //     0.0,  0.0,  0.5,  1.0,
        // );
        //

        // // ECS Singles Rendering
        // {
        //     auto registryView = _registry.view<MeshComponent, Transform>();
        //     float rotationSpeed = glm::radians(45.0f); // 45/s
        //     glm::vec3 rotationAxis = glm::vec3(0.0f, 1.0f, 0.0f); // Y-axis
        //     glm::quat deltaRot = glm::angleAxis(rotationSpeed * _deltaTime, rotationAxis);
        //     for (auto entity : registryView) {
                
        //         auto& meshComponent = registryView.get<MeshComponent>(entity);
        //         auto& transformComponent = registryView.get<Transform>(entity);
        //         auto translation = glm::translate(transformComponent.position);
        //         transformComponent.rotation = deltaRot * transformComponent.rotation;
        //         glm::mat4 rotationMat = glm::toMat4(transformComponent.rotation);

        //        // push_constants.vertexBuffer = _testMeshes[2]->meshBuffers.vertexBufferAddress;
        //         push_constants.vertexBuffer = meshComponent.mesh.get()->meshBuffers.vertexBufferAddress;
        //         push_constants.worldMatrix = projectionMatrix * viewMatrix * translation * rotationMat;

        //         // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        //         // vkCmdBindIndexBuffer(cmd, meshComponent.mesh.get()->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        //         // vkCmdDrawIndexed(cmd, meshComponent.mesh.get()->surfaces[0].count, 1, meshComponent.mesh.get()->surfaces[0].startIndex, 0, 0);
        //         vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        //         vkCmdBindIndexBuffer(cmd, meshComponent.mesh.get()->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        //         vkCmdDrawIndexed(cmd,  meshComponent.mesh.get()->surfaces[0].count, 1,  meshComponent.mesh.get()->surfaces[0].startIndex, 0, 0);
        //     }
        // }


        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _instancedMeshPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _instancedMeshPipelineLayout, 0, 1, &imageSet, 0, nullptr);
        // ECS Singles Rendering
        {
            auto t0 = Clock::now();
            //_batches.clear(); // clear previous frame data
            // exclude entities that are not meant for batch rendering
            auto registryView = _registry.view<MeshComponent, Transform>(entt::exclude<SingleRenderTag>);
            size_t offset2 = 0;


            size_t numThreads = _threadPool.threadCount;
            size_t numEntities = std::distance(registryView.begin(), registryView.end()); //1000000; // doesnt matter if it is accurate since this is just for load balancing
            size_t chunkSize = (numEntities + numThreads - 1) / numThreads;

            for (auto entity : registryView) {
                auto& meshComponent = registryView.get<MeshComponent>(entity);
                auto& batch = _batches[meshComponent.mesh.get()];
                batch.resize(numEntities);
                break;
            }
            std::vector<std::unordered_map<MeshAsset*, std::vector<InstanceData>>> threadLocalBatches(numThreads);  

            auto entitiesBegin = registryView.begin();

            for (size_t t = 0; t < numThreads; ++t) {
                _threadPool.Enqueue([&, t]() {
                    size_t start = t * chunkSize;
                    size_t end = std::min(start + chunkSize, numEntities);

                    auto it = entitiesBegin;
                    std::advance(it, start);

                    for (size_t i = start; i < end; ++i, ++it) {
                        auto entity = *it;
                        auto& trans = registryView.get<Transform>(entity);

                        InstanceData instance{};
                        instance.position = trans.position;
                        instance.rotation = trans.rotation;
                        instance.scale    = trans.scale;

                        // same write as before
                        _batches.begin()->second[i] = instance;
                    }
                });
            }

            // wait instead of join
            _threadPool.Wait();

            auto t1 = Clock::now();

            size_t offset = 0; // starting point in the instance buffer
            for (auto& [mesh, instances] : _batches) {
                size_t dataSize = instances.size() * sizeof(InstanceData);

                // Copy CPU-side instances into the persistently mapped GPU buffer
                memcpy(static_cast<char*>(_instanceBuffer.info.pMappedData) + offset,
                    instances.data(),
                    dataSize);

                auto t2 = Clock::now();

                // Save GPU device address for push constants
                VkDeviceAddress instanceAddress = _instanceBuffer.deviceAddress + offset;

                // Push constants per mesh
                BatchDrawPushConstants pc{};
                pc.viewProjection = projectionMatrix * viewMatrix;
                pc.vertexBuffer = mesh->meshBuffers.vertexBufferAddress;
                pc.instanceBuffer = instanceAddress;

                vkCmdPushConstants(cmd, _instancedMeshPipelineLayout,
                                VK_SHADER_STAGE_VERTEX_BIT,
                                0,
                                sizeof(pc),
                                &pc);

                // Bind index buffer for this mesh
                vkCmdBindIndexBuffer(cmd, mesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

                // Draw all instances of this mesh
                vkCmdDrawIndexed(cmd,
                                mesh->surfaces[0].count,    // index count per mesh
                                instances.size(),           // number of instances
                                mesh->surfaces[0].startIndex,
                                0,
                                0);

                //offset += dataSize; // move pointer for the next batch
                auto t3 = Clock::now();
                auto ms = [](auto a, auto b) {
                    return std::chrono::duration<float, std::milli>(b - a).count();
                };
                //printf("Build: %.2f ms | Upload: %.2f ms | Record: %.2f ms\n",
                //ms(t0,t1), ms(t1,t2), ms(t2,t3));
            }
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);
        // // Single entity rendering
        auto registryViewSingles = _registry.view<MeshComponent, Transform, SingleRenderTag>();

          for (auto entity : registryViewSingles) {
                auto& meshComponent = registryViewSingles.get<MeshComponent>(entity);
                auto& transformComponent = registryViewSingles.get<Transform>(entity);
                // TRS Matrix
                glm::mat4 T = glm::translate(glm::mat4(1.0f), transformComponent.position);
                glm::mat4 R = glm::mat4_cast(transformComponent.rotation);
                glm::mat4 S = glm::scale(glm::mat4(1.0f), transformComponent.scale);

                glm::mat4 model = T * R * S;

                GPUDrawPushConstants push_constants;
                auto meshAssetPtr = meshComponent.mesh.get();
                push_constants.vertexBuffer = meshAssetPtr->meshBuffers.vertexBufferAddress;
                push_constants.model = model;
                push_constants.viewProjection = projectionMatrix * viewMatrix;
                vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
                vkCmdBindIndexBuffer(cmd, meshAssetPtr->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd, meshAssetPtr->surfaces[0].count, 1, meshAssetPtr->surfaces[0].startIndex, 0, 0);
        }
       // vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);
        //vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);
    //    glm::mat4 pos1 = glm::translate(glm::vec3(1.0f, 1.0f, 1.0f));
    //     glm::mat4 pos2 = glm::translate(glm::vec3(3.0f, -1.0f, -1.0f));

    //     GPUDrawPushConstants push_constants;
    //     push_constants.vertexBuffer = _testMeshes[2]->meshBuffers.vertexBufferAddress;
    //     push_constants.worldMatrix = projectionMatrix * viewMatrix * pos1;

    //     vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
    //     vkCmdBindIndexBuffer(cmd, _testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    //     vkCmdDrawIndexed(cmd, _testMeshes[2]->surfaces[0].count, 1, _testMeshes[2]->surfaces[0].startIndex, 0, 0);


        // push_constants.worldMatrix = projectionMatrix * viewMatrix * pos2;


        // vkCmdPushConstants(cmd, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);
        // vkCmdBindIndexBuffer(cmd, _testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        // vkCmdDrawIndexed(cmd, _testMeshes[2]->surfaces[0].count, 1, _testMeshes[2]->surfaces[0].startIndex, 0, 0);
        vkCmdEndRendering(cmd);
    }

    void Core::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
    {
        VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

        vkCmdBeginRendering(cmd, &renderInfo);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        vkCmdEndRendering(cmd);
    }

    void Core::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function)
    {
        VK_CHECK(vkResetFences(_device, 1, &_immFence));
        VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

        VkCommandBuffer cmd = _immCommandBuffer;

        VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

        function(cmd);

        VK_CHECK(vkEndCommandBuffer(cmd));

        VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
        VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

        // submit command buffer to the queue and execute it.
        //  _renderFence will now block until the graphic commands finish execution
        VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

        VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
    }

    void Core::InitImgui()
    {
        // 1: create descriptor pool for IMGUI
        //  the size of the pool is very oversize, but it's copied from imgui demo
        //  itself.
        VkDescriptorPoolSize pool_sizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
            { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000;
        pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;

        VkDescriptorPool imguiPool;
        VK_CHECK(vkCreateDescriptorPool(_device, &pool_info, nullptr, &imguiPool));

        // 2: initialize imgui library

        // this initializes the core structures of imgui
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        // this initializes imgui for SDL
        ImGui_ImplGlfw_InitForVulkan(_window->GetNativeHandle(), true);

        // this initializes imgui for Vulkan
        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = _instance;
        init_info.PhysicalDevice = _chosenGPU;
        init_info.Device = _device;
        init_info.Queue = _graphicsQueue;
        init_info.DescriptorPool = imguiPool;
        init_info.MinImageCount = 3;
        init_info.ImageCount = 3;
        init_info.UseDynamicRendering = true;

        //dynamic rendering parameters for imgui to use

        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapchainImageFormat;
        

        init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;


        ImGui_ImplVulkan_Init(&init_info);

        //ImGui_ImplVulkan_CreateFontsTexture();

        // add the destroy the imgui created structures
        _mainDeletionQueue.push_function([=, this]() {
            ImGui_ImplVulkan_Shutdown();
            vkDestroyDescriptorPool(_device, imguiPool, nullptr);
        });
    }

    void Core::InitTrianglePipeline()
    {
        VkShaderModule triangleFragShader;
        if (!vkutil::load_shader_module("../../../shaders/colored_triangle.frag.spv", _device, &triangleFragShader)) {
            ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");
        }

        VkShaderModule triangleVertexShader;
        if (!vkutil::load_shader_module("../../../shaders/colored_triangle.vert.spv", _device, &triangleVertexShader)) {
            ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");
        }
        
        //build the pipeline layout that controls the inputs/outputs of the shader
        //we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
        VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
        VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

        PipelineBuilder pipelineBuilder;

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
        //connecting the vertex and pixel shaders to the pipeline
        pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
        //it will draw triangles
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        //filled triangles
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        //no backface culling
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        //no multisampling
        pipelineBuilder.set_multisampling_none();
        
        //no blending
        pipelineBuilder.disable_blending();
        //pipelineBuilder.enable_blending_additive();

        //no depth testing
        //pipelineBuilder.disable_depthtest();
        pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
        //connect the image format we will draw into, from draw image

        //connect the image format we will draw into, from draw image
        pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
        pipelineBuilder.set_depth_format(_depthImage.imageFormat);

        //finally build the pipeline
        _trianglePipeline = pipelineBuilder.build_pipeline(_device);

        //clean structures
        vkDestroyShaderModule(_device, triangleFragShader, nullptr);
        vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
            vkDestroyPipeline(_device, _trianglePipeline, nullptr);
        });
    }

    void Core::InitMeshPipeline() {
        VkShaderModule triangleFragShader;
        if (!vkutil::load_shader_module("../../../shaders/colored_triangle.frag.spv", _device, &triangleFragShader))
            ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");

        VkShaderModule triangleVertexShader;
        if (!vkutil::load_shader_module("../../../shaders/colored_triangle_mesh.vert.spv", _device, &triangleVertexShader))
            ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");
        // if (!vkutil::load_shader_module("../../../shaders/batch_color_mesh.vert.spv", _device, &triangleVertexShader))
        //     ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");

        VkPushConstantRange bufferRange{};
        bufferRange.offset = 0;
        bufferRange.size = sizeof(GPUDrawPushConstants);
        bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
        pipeline_layout_info.pPushConstantRanges = &bufferRange;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pSetLayouts = &_singleImageDescriptorLayout;
	    pipeline_layout_info.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_meshPipelineLayout));

        PipelineBuilder pipelineBuilder;

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = _meshPipelineLayout;
        //connecting the vertex and pixel shaders to the pipeline
        pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
        //it will draw triangles
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        //filled triangles
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        //no backface culling
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        //no multisampling
        pipelineBuilder.set_multisampling_none();
        //no blending
        pipelineBuilder.disable_blending();
        //pipelineBuilder.enable_blending_additive();

        pipelineBuilder.disable_depthtest();
        pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

        //connect the image format we will draw into, from draw image
        pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

        //finally build the pipeline
        _meshPipeline = pipelineBuilder.build_pipeline(_device);

        //clean structures
        vkDestroyShaderModule(_device, triangleFragShader, nullptr);
        vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _meshPipeline, nullptr);
        });
    }

    void Core::InitInstancedMeshPipeline() {
        VkShaderModule triangleFragShader;
        if (!vkutil::load_shader_module("../../../shaders/colored_triangle.frag.spv", _device, &triangleFragShader))
            ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");


        VkShaderModule triangleVertexShader;
        if (!vkutil::load_shader_module("../../../shaders/batch_color_mesh.vert.spv", _device, &triangleVertexShader))
            ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");

        VkPushConstantRange bufferRange{};
        bufferRange.offset = 0;
        bufferRange.size = sizeof(BatchDrawPushConstants);
        bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
        pipeline_layout_info.pPushConstantRanges = &bufferRange;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pSetLayouts = &_singleImageDescriptorLayout;
	    pipeline_layout_info.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_instancedMeshPipelineLayout));

        PipelineBuilder pipelineBuilder;

        //use the triangle layout we created
        pipelineBuilder._pipelineLayout = _instancedMeshPipelineLayout;
        //connecting the vertex and pixel shaders to the pipeline
        pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
        //it will draw triangles
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        //filled triangles
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        //no backface culling
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        //no multisampling
        pipelineBuilder.set_multisampling_none();
        //pipelineBuilder.set_multisampling(VK_SAMPLE_COUNT_4_BIT);
        //no blending
        pipelineBuilder.disable_blending();
        //pipelineBuilder.enable_blending_additive();

        pipelineBuilder.disable_depthtest();
        pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

        //connect the image format we will draw into, from draw image
        pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
	    pipelineBuilder.set_depth_format(_depthImage.imageFormat);

        //finally build the pipeline
        _instancedMeshPipeline = pipelineBuilder.build_pipeline(_device);

        //clean structures
        vkDestroyShaderModule(_device, triangleFragShader, nullptr);
        vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _instancedMeshPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _instancedMeshPipeline, nullptr);
        });
    }

    void Core::InitDefaultData()
    {
        std::array<Vertex,4> rect_vertices;

        rect_vertices[0].position = {0.5, -0.5, 0};
        rect_vertices[1].position = {0.5, 0.5, 0};
        rect_vertices[2].position = {-0.5, -0.5, 0};
        rect_vertices[3].position = {-0.5, 0.5, 0};

        // rect_vertices[0].color = {0,0, 0,1};
        // rect_vertices[1].color = { 0.5,0.5,0.5 ,1};
        // rect_vertices[2].color = { 1,0, 0,1 };
        // rect_vertices[3].color = { 0,1, 0,1 };

        std::array<uint32_t,6> rect_indices;

        rect_indices[0] = 0;
        rect_indices[1] = 1;
        rect_indices[2] = 2;

        rect_indices[3] = 2;
        rect_indices[4] = 1;
        rect_indices[5] = 3;

        rectangle = UploadMesh(rect_indices,rect_vertices);

        //3 default textures, white, grey, black. 1 pixel each
        uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
        _whiteImage = CreateImage((void*)&white, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT);

        uint32_t grey = glm::packUnorm4x8(glm::vec4(0.66f, 0.66f, 0.66f, 1));
        _greyImage = CreateImage((void*)&grey, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT);

        uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 0));
        _blackImage = CreateImage((void*)&black, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT);

        //checkerboard image
        uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
        std::array<uint32_t, 16 *16 > pixels; //for 16x16 checkerboard texture
        for (int x = 0; x < 16; x++) {
            for (int y = 0; y < 16; y++) {
                pixels[y*16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
            }
        }
        _errorCheckerboardImage = CreateImage(pixels.data(), VkExtent3D{16, 16, 1}, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT);

        VkSamplerCreateInfo sampl = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

        sampl.magFilter = VK_FILTER_NEAREST;
        sampl.minFilter = VK_FILTER_NEAREST;

        vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerNearest);

        sampl.magFilter = VK_FILTER_LINEAR;
        sampl.minFilter = VK_FILTER_LINEAR;
        vkCreateSampler(_device, &sampl, nullptr, &_defaultSamplerLinear);

        _mainDeletionQueue.push_function([&](){
            vkDestroySampler(_device,_defaultSamplerNearest,nullptr);
            vkDestroySampler(_device,_defaultSamplerLinear,nullptr);

            DestroyImage(_whiteImage);
            DestroyImage(_greyImage);
            DestroyImage(_blackImage);
            DestroyImage(_errorCheckerboardImage);
        });



        //delete the rectangle data on engine shutdown
        _mainDeletionQueue.push_function([&](){
            DestroyBuffer(rectangle.indexBuffer);
            DestroyBuffer(rectangle.vertexBuffer);
        });

        _testMeshes = LoadGltfMeshes(this,"../../../assets/basicmesh.glb").value();
        auto tempMeshes = LoadGltfMeshes(this,"../../../assets/tetrahedron.glb").value();
        _testMeshes.insert(
            _testMeshes.end(),              // insert at the end of _testMeshes
            tempMeshes.begin(),             // start of tempMeshes
            tempMeshes.end()                // end of tempMeshes
        );
    //     for (auto& meshAsset : _testMeshes) {
    //     int gap = 5;
    //     for (int x = 0; x < 100; x++) {
    //         for (int y = 0; y < 100; y++) {
    //             for (int z = 0; z < 100; z++) {
    //                 auto meshEntity = _registry.create();
    //                 _registry.emplace<MeshComponent>(meshEntity, _testMeshes[0]);
    //                 auto transform = Transform();
    //                 transform.position = glm::vec3 {x * gap, y * gap,  z * gap};
    //                 //transform.scale = glm::vec3 {10.0f, 10.0f, 10.0f};
    //                 _registry.emplace<Transform>(meshEntity, transform);
    //             }
    //         }
    //     }
    //    }

        // destroy mesh buffers on shutdown
        _mainDeletionQueue.push_function([&]() {
            for (auto& mesh : _testMeshes) {
                DestroyBuffer(mesh->meshBuffers.vertexBuffer);
                DestroyBuffer(mesh->meshBuffers.indexBuffer);
            }
        });
    }

    std::optional<std::vector<std::shared_ptr<MeshAsset>>> Core::LoadGltfMeshes(Core* engine, std::filesystem::path filePath)
    {
        auto dataResult = fastgltf::GltfDataBuffer::FromPath(filePath);
        if (!dataResult) {
            return std::nullopt;
        }

        fastgltf::GltfDataBuffer data = std::move(dataResult.get());

        constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;

        fastgltf::Asset gltf;
        fastgltf::Parser parser;
        auto load = parser.loadGltfBinary(data, filePath.parent_path(), gltfOptions);

        if (!load) {
            return std::nullopt;
        }

        gltf = std::move(load.get());

        std::vector<std::shared_ptr<MeshAsset>> meshes;

        for (fastgltf::Mesh& mesh : gltf.meshes) {
            MeshAsset newMesh;
            newMesh.name = mesh.name;

            std::vector<uint32_t> indices;
            std::vector<Vertex> vertices;

            for (auto&& p : mesh.primitives) {
                // Skip primitives without indices
                if (!p.indicesAccessor.has_value()) {
                    continue;
                }

                GeoSurface newSurface{};
                newSurface.startIndex = static_cast<uint32_t>(indices.size());

                const auto indexAccessorIndex = p.indicesAccessor.value();
                auto& indexAccessor = gltf.accessors[indexAccessorIndex];

                // Debug-safe: indices must be SCALAR
                if (indexAccessor.type != fastgltf::AccessorType::Scalar) {
                    continue;
                }

                newSurface.count = static_cast<uint32_t>(indexAccessor.count);

                const size_t initial_vtx = vertices.size();

                // -------------------------
                // POSITION (required)
                // -------------------------
                fastgltf::Attribute* positionAttr = p.findAttribute("POSITION");
                if (!positionAttr) {
                    continue;
                }

                auto& positionAccessor = gltf.accessors[positionAttr->accessorIndex];

                // Debug-safe: POSITION must be VEC3
                if (positionAccessor.type != fastgltf::AccessorType::Vec3) {
                    continue;
                }

                vertices.resize(vertices.size() + positionAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(
                    gltf,
                    positionAccessor,
                    [&](glm::vec3 pos, size_t index) {
                        Vertex v{};
                        v.position = pos;
                        v.normal = glm::vec3(1.0f, 0.0f, 0.0f);
                        v.uv_x = 0.0f;
                        v.uv_y = 0.0f;
                        vertices[initial_vtx + index] = v;
                    });

                // -------------------------
                // INDICES
                // -------------------------
                indices.reserve(indices.size() + indexAccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(
                    gltf,
                    indexAccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + static_cast<uint32_t>(initial_vtx));
                    });

                // -------------------------
                // NORMALS (optional)
                // -------------------------
                if (auto* normalsAttr = p.findAttribute("NORMAL")) {
                    auto& normalAccessor = gltf.accessors[normalsAttr->accessorIndex];

                    if (normalAccessor.type == fastgltf::AccessorType::Vec3) {
                        fastgltf::iterateAccessorWithIndex<glm::vec3>(
                            gltf,
                            normalAccessor,
                            [&](glm::vec3 normal, size_t index) {
                                vertices[initial_vtx + index].normal = normal;
                            });
                    }
                }

                // -------------------------
                // UVs (optional)
                // -------------------------
                if (auto* uvsAttr = p.findAttribute("TEXCOORD_0")) {
                    auto& uvAccessor = gltf.accessors[uvsAttr->accessorIndex];

                    if (uvAccessor.type == fastgltf::AccessorType::Vec2) {
                        fastgltf::iterateAccessorWithIndex<glm::vec2>(
                            gltf,
                            uvAccessor,
                            [&](glm::vec2 uv, size_t index) {
                                vertices[initial_vtx + index].uv_x = uv.x;
                                vertices[initial_vtx + index].uv_y = uv.y;
                            });
                    }
                }

                newMesh.surfaces.push_back(newSurface);
            }

            newMesh.meshBuffers = engine->UploadMesh(indices, vertices);
            meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
        }

        return meshes;
    }

    // std::optional<std::vector<std::shared_ptr<MeshAsset>>> Core::LoadGltfMeshes(Core *engine, std::filesystem::path filePath)
    // {
    //     //ENGINE_LOG_INFO("Loading GLTF: %s", filePath);

    //     auto dataResult = fastgltf::GltfDataBuffer::FromPath(filePath);
    //     if (!dataResult) {
    //         //ENGINE_LOG_INFO("Failed to load glTF file");
    //         return std::nullopt;
    //     }
    //     fastgltf::GltfDataBuffer data = std::move(dataResult.get());

    //     constexpr auto gltfOptions =
    //         fastgltf::Options::LoadExternalBuffers;

    //     fastgltf::Asset gltf;
    //     fastgltf::Parser parser;
    //     auto load = parser.loadGltfBinary(data, filePath.parent_path(), gltfOptions);

    //     if (load) {
    //         gltf = std::move(load.get());
    //     } else {
    //         //ENGINE_LOG_INFO("Failed to load glTF: %s \n", fastgltf::to_underlying(load.error()));
    //         return std::nullopt;
    //     }

    //     std::vector<std::shared_ptr<MeshAsset>> meshes;
    //     std::vector<uint32_t> indices;
    //     std::vector<Vertex> vertices;

    //     for (fastgltf::Mesh& mesh : gltf.meshes) {
    //         MeshAsset newMesh;
    //         newMesh.name = mesh.name;

    //         // clear the mesh arrays each mesh, we dont want to merge them by error
    //         indices.clear();
    //         vertices.clear();
            
    //         for (auto&& p : mesh.primitives) {
    //             GeoSurface newSurface;
    //             newSurface.startIndex = (uint32_t)indices.size();
    //             newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

    //             size_t initial_vtx = vertices.size();
    //             // load indexes
    //             {
    //                 fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
    //                 indices.reserve(indices.size() + indexaccessor.count);

    //                 fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
    //                     [&](std::uint32_t idx) {
    //                         indices.push_back(idx + initial_vtx);
    //                     });
    //             }

    //             // load vertex positions
    //             {
    //                 fastgltf::Attribute* positionAttr = p.findAttribute("POSITION");
    //                 if (!positionAttr) {
    //                     continue;
    //                 }

    //                 auto& positionAccessor = gltf.accessors[positionAttr->accessorIndex];
    //                 vertices.resize(vertices.size() + positionAccessor.count);

    //                 fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, positionAccessor,
    //                     [&](glm::vec3 v, size_t index) {
    //                         Vertex newvtx;
    //                         newvtx.position = v;
    //                         newvtx.normal = { 1, 0, 0 };
    //                         //newvtx.color = glm::vec4 { 1.f };
    //                         newvtx.uv_x = 0;
    //                         newvtx.uv_y = 0;
    //                         vertices[initial_vtx + index] = newvtx;
    //                     });
    //             }

    //             // load vertex normals
    //             auto normalsAttr = p.findAttribute("NORMAL");
    //             if (normalsAttr) {
    //                 auto& normalAccessor = gltf.accessors[normalsAttr->accessorIndex];

    //                 fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, normalAccessor,
    //                     [&](glm::vec3 v, size_t index) {
    //                         vertices[initial_vtx + index].normal = v;
    //                     });
    //             }

    //              // load UVs
    //             auto uvsAttr = p.findAttribute("TEXCOORD_0");
    //             if (uvsAttr) {
    //                 auto& uvAccessor = gltf.accessors[uvsAttr->accessorIndex];

    //                 fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, uvAccessor,
    //                     [&](glm::vec2 v, size_t index) {
    //                         vertices[initial_vtx + index].uv_x = v.x;
    //                         vertices[initial_vtx + index].uv_y = v.y;
    //                     });
    //             }
    //             // // load vertex colors
    //             // auto colorsAttr = p.findAttribute("COLOR_0");
    //             // if (colorsAttr) {
    //             //     auto& colorsAccessor = gltf.accessors[colorsAttr->accessorIndex];

    //             //     if (colorsAccessor.type == fastgltf::AccessorType::Vec4) {
    //             //         fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, colorsAccessor,
    //             //             [&](glm::vec4 v, size_t index) {
    //             //                 vertices[initial_vtx + index].color = v;
    //             //             });
    //             //     } else if (colorsAccessor.type == fastgltf::AccessorType::Vec3) {
    //             //         fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, colorsAccessor,
    //             //             [&](glm::vec3 v, size_t index) {
    //             //                 vertices[initial_vtx + index].color = glm::vec4(v, 1.0f);
    //             //             });
    //             //     } else {
    //             //         //ENGINE_LOG_WARN("Unsupported vertex color type in mesh %s", mesh.name.c_str());
    //             //     }
    //             // }
    //             newMesh.surfaces.push_back(newSurface);
    //         }

    //         // display the vertex normals
    //         // constexpr bool OverrideColors = true;
    //         // if (OverrideColors) {
    //         //     for (Vertex& vtx : vertices) {
    //         //         vtx.color = glm::vec4(vtx.normal, 1.f);
    //         //     }
    //         // }
    //         newMesh.meshBuffers = engine->UploadMesh(indices, vertices);
    //         meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));

    //     }
    //     return meshes;
    // }

    Core::Core()
    {
    }

    void Core::Init()
    {
#ifdef DEBUG
        ENGINE_LOG_INFO("Engine initializing in DEBUG mode!");
    #else
        ENGINE_LOG_INFO("Engine initializing in RELEASE mode!");
    #endif
        // temp camera set up
        auto cameraEntity = _registry.create();
        auto& cameraTransform = _registry.emplace<Transform>(cameraEntity);
        cameraTransform.position = { 0.0f, 0.0f, 10.0f };
        cameraTransform.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        auto& camera = _registry.emplace<Camera>(cameraEntity);

        auto inputEntity = _registry.create();
        _registry.emplace<InputState>(inputEntity);



        // Sets up GLFW Window with Vulkan Prerequisites
        InitWindow();
        InitVulkan();
        InitQueries();
        InitSwapchain();
        InitImgui();
        InitCommands();
        InitSyncStructures();
        InitDescriptors();
        InitPipelines();
        InitDefaultData();

                //_ecsDebugger = EcsDebugger(&_registry);
        _systems.push_back(std::make_unique<EcsDebugger>(_registry));
        _systems.push_back(std::make_unique<InputSystem>(_registry, inputEntity, _window.get()));
        _systems.push_back(std::make_unique<CameraSystem>(_registry));
        

        //everything went fine
        _isInitialized = true;
    }

    void Core::Run() {
        constexpr auto targetMinimizedFrameDuration = std::chrono::milliseconds(); // 20 FPS while minimized
        constexpr float fixedDelta = 1.0f / 60.0f; // 60 Hertz
        float fixedUpdateAccumulator = 0.0f;

        ENGINE_LOG_INFO("Starting Engine Main Loop.");
        bool running = true;

        // initialize frame time
        auto previousFrameTime = std::chrono::high_resolution_clock::now();
        while (!_window->ShouldClose()) {
            auto frameStartTime = std::chrono::high_resolution_clock::now();
            std::chrono::duration<float> delta = frameStartTime - previousFrameTime;
            _deltaTime = delta.count();
            //set for next frame
            previousFrameTime = frameStartTime;

            // fixed update loop TODO verify if it should run before minimize check
            _window->PollEvents();
            fixedUpdateAccumulator += _deltaTime;
            if (fixedUpdateAccumulator >= fixedDelta) {
                for (auto& system : _systems) {
                    system->FixedUpdate(fixedDelta);
                }
                fixedUpdateAccumulator -= fixedDelta;
            }

            // update loop
            if (_window->WasResized()) {
                uint32_t width = _window->GetWidth();
                uint32_t height = _window->GetHeight();
                if ((_window->GetWidth() == 0 || _window->GetHeight() == 0) || glfwGetWindowAttrib(_window->GetNativeHandle(), GLFW_ICONIFIED)) {
                    _appMinimized = true; // ignore swapchain
                }
                else {
                    _appMinimized = false;
                    // resize swapchain
                    RecreateSwapchain(width, height);
                }
            }
            if (_appMinimized) {
                auto frameEndTime = std::chrono::high_resolution_clock::now();
                auto frameDuration = frameEndTime - frameStartTime;
                auto sleepDuration = targetMinimizedFrameDuration - frameDuration;

                if (sleepDuration > std::chrono::milliseconds(0))
                {
                    std::this_thread::sleep_for(sleepDuration);
                }
                continue;
            }

            for (auto& system : _systems) {
                system->Update(_deltaTime);
            }

            // imgui new frame
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

            //some imgui UI to test
            //ImGui::ShowDemoWindow();
           // ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

            //make imgui calculate internal draw structures
            //ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            //ImGui::End();
            DrawUi();
            for (auto& system : _systems) {
                system->DrawUi();
            }
            ImGui::Render();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            
            Draw();
        }
        ENGINE_LOG_INFO("Engine Shutting Down.");
        Shutdown();
    }

    void Core::Shutdown() {
        if (!_isInitialized)
            return;

        _threadPool.~ThreadPool(); // destroy thread pool
        vkDeviceWaitIdle(_device);

        // 1 destroy per frame resources
        for (int i = 0; i < FRAME_OVERLAP; i++) {
            // destroy command pool
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
            // destroy fence
            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            // deletion queuq
            _frames[i]._deletionQueue.flush();
        }
        // 2 destroy semaphores
        for (int i = 0; i < FRAME_OVERLAP; i++) {
            vkDestroySemaphore(_device, _imageAvailableSemaphores[i], nullptr);
        }

        for (int i = 0; i < _renderFinishedSemaphores.size(); i++) {
            vkDestroySemaphore(_device, _renderFinishedSemaphores[i], nullptr);
        }

        // 3 destroy main deletion queue
        CleanupDrawImageDescriptors();
        CleanupDrawImages();
        DestroyBuffer(_instanceBuffer);
        _mainDeletionQueue.flush();

        // 4 destroy swapchain images
        CleanupSwapchainResources();

        // 5 destroy surface and device
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
    
        // 6 destroy debug messenger
        vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
        vkDestroyInstance(_instance, nullptr);
        

        _mainDeletionQueue.flush();
        //vkDestroyShaderModule(_device, computeDrawShader, nullptr);

        // 7 shutdown GLFW library
        _window->ShutdownGLFW();
    }

    entt::registry &Core::GetRegistry()
    {
        return _registry;
    }

    void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type) {
        VkDescriptorSetLayoutBinding newbind {};
        newbind.binding = binding;
        newbind.descriptorCount = 1;
        newbind.descriptorType = type;

        bindings.push_back(newbind);
    }

    void DescriptorLayoutBuilder::Clear() {
        bindings.clear();
    }
    VkDescriptorSetLayout DescriptorLayoutBuilder::Build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext, VkDescriptorSetLayoutCreateFlags flags) {
        for (auto& b : bindings) {
            b.stageFlags |= shaderStages;
        }

        VkDescriptorSetLayoutCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        info.pNext = pNext;

        info.pBindings = bindings.data();
        info.bindingCount = (uint32_t)bindings.size();
        info.flags = flags;

        VkDescriptorSetLayout set;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));

        return set;
    }

    void DescriptorAllocator::InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
    {
        std::vector<VkDescriptorPoolSize> poolSizes;
        for (PoolSizeRatio ratio : poolRatios) {
            poolSizes.push_back(VkDescriptorPoolSize{
                .type = ratio.type,
                .descriptorCount = uint32_t(ratio.ratio * maxSets)
            });
        }

        VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.flags = 0;
        pool_info.maxSets = maxSets;
        pool_info.poolSizeCount = (uint32_t)poolSizes.size();
        pool_info.pPoolSizes = poolSizes.data();

        vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
    }

    void DescriptorAllocator::ClearDescriptor(VkDevice device)
    {
        vkResetDescriptorPool(device, pool, 0);
    }

    void DescriptorAllocator::DestroyPool(VkDevice device)
    {
        vkDestroyDescriptorPool(device, pool, nullptr);
    }

    VkDescriptorSet DescriptorAllocator::Allocate(VkDevice device, VkDescriptorSetLayout layout)
    {
        VkDescriptorSetAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocInfo.pNext = nullptr;
        allocInfo.descriptorPool = pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet ds;
        VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

        return ds;
    }
    // void DescriptorAllocator::InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
    // {
    //     std::vector<VkDescriptorPoolSize> poolSizes;
    //     for (PoolSizeRatio ratio : poolRatios) {
    //         poolSizes.push_back(VkDescriptorPoolSize{
    //             .type = ratio.type,
    //             .descriptorCount = uint32_t(ratio.ratio * maxSets)
    //         });
    //     }

    //     VkDescriptorPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    //     pool_info.flags = 0;
    //     pool_info.maxSets = maxSets;
    //     pool_info.poolSizeCount = (uint32_t)poolSizes.size();
    //     pool_info.pPoolSizes = poolSizes.data();

    //     vkCreateDescriptorPool(device, &pool_info, nullptr, &pool);
    // }

    // void DescriptorAllocator::ClearDescriptor(VkDevice device)
    // {
    //     vkResetDescriptorPool(device, pool, 0);
    // }

    // void DescriptorAllocator::DestroyPool(VkDevice device)
    // {
    //     vkDestroyDescriptorPool(device,pool,nullptr);
    // }
    // VkDescriptorSet DescriptorAllocator::Allocate(VkDevice device, VkDescriptorSetLayout layout)
    // {
    //     VkDescriptorSetAllocateInfo allocInfo = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    //     allocInfo.pNext = nullptr;
    //     allocInfo.descriptorPool = pool;
    //     allocInfo.descriptorSetCount = 1;
    //     allocInfo.pSetLayouts = &layout;

    //     VkDescriptorSet ds;
    //     VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));

    void GLTFMetallic_Roughness::BuildPipelines(Core *engine)
    {
    }

    void GLTFMetallic_Roughness::ClearResources(VkDevice device)
    {
    }

    ThreadPool::ThreadPool(size_t numThreads)
    {
        threadCount = numThreads;
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> job;

                    {
                        std::unique_lock lock(mutex);
                        cv.wait(lock, [&]() { return stop || !jobs.empty(); });

                        if (stop && jobs.empty())
                            return;

                        job = std::move(jobs.front());
                        jobs.pop();
                        activeJobs++;
                    }

                    job();

                    {
                        std::unique_lock lock(mutex);
                        activeJobs--;
                        if (jobs.empty() && activeJobs == 0)
                            doneCv.notify_one();
                    }
                }
            });
        }
    }

    ThreadPool::~ThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }

        cv.notify_all();

        for (auto& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
    }

    void ThreadPool::Enqueue(std::function<void()> job)
    {
        {
            std::lock_guard lock(mutex);
            jobs.push(std::move(job));
        }
        cv.notify_one();
    }

    void ThreadPool::Wait()
    {
        std::unique_lock lock(mutex);
        doneCv.wait(lock, [&]() {
            return jobs.empty() && activeJobs == 0;
        });
    }

//     return ds;
// }
} // namespace engine