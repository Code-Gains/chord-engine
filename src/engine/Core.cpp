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
#include "NameComponent.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_descriptors.h"
#pragma clang diagnostic pop

// ECS PORT
#include "EcsDebugger.h"
#include "SunlightComponent.h"

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
        vkDestroyDescriptorSetLayout(_device, _multiImageDescriptorLayout , nullptr);
        vkDestroyDescriptorSetLayout(_device, _environmentDescriptorLayout , nullptr);
        vkDestroyDescriptorSetLayout(_device, _skyboxDescriptorLayout , nullptr);
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
        //InitTrianglePipeline();

        size_t maxInstances = 1000000; // upper bound
        _instanceBuffer = CreateBuffer(
            sizeof(InstanceData) * maxInstances,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        InitBRDFLUTPipeline();
        InitIrradiancePipeline();
        InitPrefilterPipeline();
        InitMeshPipeline();
        InitInstancedMeshPipeline();
        InitSkyboxPipeline();
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
        // 1. Create the descriptor pool (keep it alive for runtime)
        std::vector<DescriptorAllocator::PoolSizeRatio> sizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 }
        };

        globalDescriptorAllocator.InitPool(_device, 10, sizes);

        // 2. Create the descriptor set layout
        {
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            _drawImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_COMPUTE_BIT);
        }

        // // + add gpu scene data descriptor layout
        // {
        //     DescriptorLayoutBuilder builder;
        //     builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        //     _gpuSceneDataDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        // }

        // +
        {
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            _singleImageDescriptorLayout = builder.Build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
        }
        // multiple material textures
        {
            DescriptorLayoutBuilder builder;

            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // base color
            builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // normal
            builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // metallic roughness
            builder.AddBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // occlusion
            builder.AddBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // emissive

            _multiImageDescriptorLayout =
                builder.Build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
        }
        // scene data: set 1
        {
            DescriptorLayoutBuilder builder;

            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

            _gpuSceneDataDescriptorLayout =
                builder.Build(
                    _device,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
                );
        }
        // reflection data: set 2
        {
            DescriptorLayoutBuilder builder;

            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // irradiance later
            builder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // prefiltered/env
            builder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER); // BRDF LUT

            _environmentDescriptorLayout =
                builder.Build(
                    _device,
                    VK_SHADER_STAGE_FRAGMENT_BIT
                );
        }
        { // skybox
            DescriptorLayoutBuilder builder;
            builder.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

            _skyboxDescriptorLayout =
                builder.Build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
        }

        // 3. Allocate the descriptor set
        _drawImageDescriptors = globalDescriptorAllocator.Allocate(_device, _drawImageDescriptorLayout);


        for (int i = 0; i < FRAME_OVERLAP; i++) {
            // create a descriptor pool
            std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> frame_sizes = { 
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32 },
            };

            _frames[i]._frameDescriptors = DescriptorAllocatorGrowable{};
            _frames[i]._frameDescriptors.init(_device, 1000, frame_sizes);
        
            _mainDeletionQueue.push_function([&, i]() {
                _frames[i]._frameDescriptors.destroy_pools(_device);
            });
        }

        // 4. Update descriptor set to point to the current draw image
        UpdateDrawImageDescriptor();
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
        VkSampleCountFlagBits samples)
    {
        AllocatedImage newImage;
        newImage.imageFormat = format;
        newImage.imageExtent = size;

        if (data != nullptr) {
            usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        }

        VkImageCreateInfo img_info = vkinit::image_create_info(format, usage, size);
        img_info.samples = samples;

        if (mipmapped) {
            img_info.mipLevels =
                static_cast<uint32_t>(
                    std::floor(std::log2(std::max(size.width, size.height)))
                ) + 1;
        } else {
            img_info.mipLevels = 1;
        }

        VmaAllocationCreateInfo allocinfo{};
        allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocinfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        VK_CHECK(vmaCreateImage(
            _allocator,
            &img_info,
            &allocinfo,
            &newImage.image,
            &newImage.allocation,
            nullptr
        ));

        VkImageAspectFlags aspectFlag = VK_IMAGE_ASPECT_COLOR_BIT;
        if (format == VK_FORMAT_D32_SFLOAT ||
            format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
            format == VK_FORMAT_D24_UNORM_S8_UINT) {
            aspectFlag = VK_IMAGE_ASPECT_DEPTH_BIT;
        }

        if (data != nullptr) {
            const size_t pixelSize = 4; // valid for VK_FORMAT_R8G8B8A8_UNORM
            const size_t dataSize =
                size.width * size.height * size.depth * pixelSize;

            AllocatedBuffer uploadBuffer = CreateBuffer(
                dataSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU
            );

            memcpy(uploadBuffer.allocation->GetMappedData(), data, dataSize);

            ImmediateSubmit([&](VkCommandBuffer cmd) {
                vkutil::transition_image(
                    cmd,
                    newImage.image,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
                );

                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = 0;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;

                copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegion.imageSubresource.mipLevel = 0;
                copyRegion.imageSubresource.baseArrayLayer = 0;
                copyRegion.imageSubresource.layerCount = 1;

                copyRegion.imageExtent = size;

                vkCmdCopyBufferToImage(
                    cmd,
                    uploadBuffer.buffer,
                    newImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &copyRegion
                );

                vkutil::transition_image(
                    cmd,
                    newImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                );
            });

            DestroyBuffer(uploadBuffer);
        }

        VkImageViewCreateInfo view_info =
            vkinit::imageview_create_info(format, newImage.image, aspectFlag);

        view_info.subresourceRange.levelCount = img_info.mipLevels;

        VK_CHECK(vkCreateImageView(
            _device,
            &view_info,
            nullptr,
            &newImage.imageView
        ));

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

        //allocate a new uniform buffer for the scene data
        AllocatedBuffer gpuSceneDataBuffer = CreateBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        //add it to the deletion queue of this frame so it gets deleted once its been used
        GetCurrentFrame()._deletionQueue.push_function([=, this]() {
            DestroyBuffer(gpuSceneDataBuffer);
        });

        auto sunlightView = _registry.view<SunlightComponent>();
        if (!sunlightView.empty()) {
            auto lightEntity = *sunlightView.begin();
            auto& sunlight = sunlightView.get<SunlightComponent>(lightEntity);

            sceneData.sunlightDirection =
                glm::vec4(
                    glm::normalize(sunlight.direction),
                    sunlight.intensity
                );
            sceneData.ambientColor =
                glm::vec4(
                    sunlight.color,
                    sunlight.ambient
                );
        }

        auto cameraView = _registry.view<Camera, Transform>();
        if (cameraView.begin() != cameraView.end()) {
            auto cameraEntity = *cameraView.begin();
            auto& transform = cameraView.get<Transform>(cameraEntity);
            sceneData.cameraPosition = glm::vec4(transform.position, 1.0f);
        }

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

                vkCmdBindPipeline(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _skyboxPipeline
        );

        glm::mat4 skyboxView = glm::mat4(glm::mat3(viewMatrix));
        glm::mat4 skyboxVP = projectionMatrix * skyboxView;

        vkCmdPushConstants(
            cmd,
            _skyboxPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &skyboxVP
        );

        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _skyboxPipelineLayout,
            0,
            1,
            &_skyboxCubemap.descriptorSet,
            0,
            nullptr
        );

        vkCmdDraw(cmd, 36, 1, 0, 0);


        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _instancedMeshPipeline);
        // ECS Batch Rendering
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
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _meshPipelineLayout,
            2,
            1,
            &_environmentDescriptorSet,
            0,
            nullptr
        );
        //vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);
        // ECS Singles rendering
        auto registryViewSingles = _registry.view<MeshComponent, Transform, SingleRenderTag>();

        for (auto entity : registryViewSingles) {
            auto& meshComponent = registryViewSingles.get<MeshComponent>(entity);
            auto& transformComponent = registryViewSingles.get<Transform>(entity);

            auto meshAssetPtr = meshComponent.mesh.get();
            auto& surface = meshAssetPtr->surfaces[0];
            auto* material = surface.material;

            AllocatedImage* baseColor = material ? material->image : nullptr;
            AllocatedImage* normal = material ? material->normalImage : nullptr;
            AllocatedImage* metallicRoughness = material ? material->metallicRoughnessImage : nullptr;
            AllocatedImage* occlusion = material ? material->occlusionImage : nullptr;
            AllocatedImage* emissive = material ? material->emissionImage : nullptr;

            if (!baseColor) baseColor = &_errorCheckerboardImage;
            if (!normal) normal = &_errorCheckerboardImage;
            if (!metallicRoughness) metallicRoughness = &_whiteImage;
            if (!occlusion) occlusion = &_whiteImage;
            if (!emissive) emissive = &_blackImage;

            VkDescriptorSet imageSet =
                GetCurrentFrame()._frameDescriptors.allocate(
                    _device,
                    _multiImageDescriptorLayout
                );

            DescriptorWriter imageWriter;
            imageWriter.write_image(
                0,
                baseColor->imageView,
                baseColor->sampler != VK_NULL_HANDLE ? baseColor->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                1,
                normal->imageView,
                normal->sampler != VK_NULL_HANDLE ? normal->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                2,
                metallicRoughness->imageView,
                metallicRoughness->sampler != VK_NULL_HANDLE ? metallicRoughness->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                3,
                occlusion->imageView,
                occlusion->sampler != VK_NULL_HANDLE ? occlusion->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                4,
                emissive->imageView,
                emissive->sampler != VK_NULL_HANDLE ? emissive->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.update_set(_device, imageSet);

            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                _meshPipelineLayout,
                0,
                1,
                &imageSet,
                0,
                nullptr
            ); 
            
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                _meshPipelineLayout,
                1,
                1,
                &globalDescriptor,
                0,
                nullptr
            );

            glm::mat4 T = glm::translate(glm::mat4(1.0f), transformComponent.position);
            glm::mat4 R = glm::mat4_cast(transformComponent.rotation);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), transformComponent.scale);
            glm::mat4 model = T * R * S;

            GPUDrawPushConstants push_constants;
            push_constants.vertexBuffer = meshAssetPtr->meshBuffers.vertexBufferAddress;
            push_constants.model = model;
            push_constants.viewProjection = projectionMatrix * viewMatrix;

            vkCmdPushConstants(
                cmd,
                _meshPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(GPUDrawPushConstants),
                &push_constants
            );

            vkCmdBindIndexBuffer(
                cmd,
                meshAssetPtr->meshBuffers.indexBuffer.buffer,
                0,
                VK_INDEX_TYPE_UINT32
            );

            vkCmdDrawIndexed(
                cmd,
                surface.count,
                1,
                surface.startIndex,
                0,
                0
            );
        }
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

    // void Core::InitTrianglePipeline()
    // {
    //     VkShaderModule triangleFragShader;
    //     if (!vkutil::load_shader_module("../../../shaders/colored_triangle.frag.spv", _device, &triangleFragShader)) {
    //         ENGINE_LOG_ERROR("Error when building the triangle fragment shader module");
    //     }

    //     VkShaderModule triangleVertexShader;
    //     if (!vkutil::load_shader_module("../../../shaders/colored_triangle.vert.spv", _device, &triangleVertexShader)) {
    //         ENGINE_LOG_ERROR("Error when building the triangle vertex shader module");
    //     }
        
    //     //build the pipeline layout that controls the inputs/outputs of the shader
    //     //we are not using descriptor sets or other systems yet, so no need to use anything other than empty default
    //     VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
    //     VK_CHECK(vkCreatePipelineLayout(_device, &pipeline_layout_info, nullptr, &_trianglePipelineLayout));

    //     PipelineBuilder pipelineBuilder;

    //     //use the triangle layout we created
    //     pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
    //     //connecting the vertex and pixel shaders to the pipeline
    //     pipelineBuilder.set_shaders(triangleVertexShader, triangleFragShader);
    //     //it will draw triangles
    //     pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    //     //filled triangles
    //     pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
    //     //no backface culling
    //     pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    //     //no multisampling
    //     pipelineBuilder.set_multisampling_none();
        
    //     //no blending
    //     pipelineBuilder.disable_blending();
    //     //pipelineBuilder.enable_blending_additive();

    //     //no depth testing
    //     //pipelineBuilder.disable_depthtest();
    //     pipelineBuilder.enable_depthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);
    //     //connect the image format we will draw into, from draw image

    //     //connect the image format we will draw into, from draw image
    //     pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
    //     pipelineBuilder.set_depth_format(_depthImage.imageFormat);

    //     //finally build the pipeline
    //     _trianglePipeline = pipelineBuilder.build_pipeline(_device);

    //     //clean structures
    //     vkDestroyShaderModule(_device, triangleFragShader, nullptr);
    //     vkDestroyShaderModule(_device, triangleVertexShader, nullptr);

    //     _mainDeletionQueue.push_function([&]() {
    //         vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
    //         vkDestroyPipeline(_device, _trianglePipeline, nullptr);
    //     });
    // }

    void Core::InitMeshPipeline() {
        assert(_multiImageDescriptorLayout != VK_NULL_HANDLE);
        assert(_gpuSceneDataDescriptorLayout != VK_NULL_HANDLE);
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

        VkDescriptorSetLayout setLayouts[] = {
            _multiImageDescriptorLayout,      // set 0
            _gpuSceneDataDescriptorLayout,    // set 1
            _environmentDescriptorLayout      // set 2
        };

        VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
        pipeline_layout_info.pPushConstantRanges = &bufferRange;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pSetLayouts = setLayouts;
        pipeline_layout_info.setLayoutCount = 3;
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

    void Core::InitSkyboxPipeline()
    {
        VkShaderModule skyboxFragShader;
        if (!vkutil::load_shader_module("../../../shaders/skybox.frag.spv", _device, &skyboxFragShader))
            ENGINE_LOG_ERROR("Error when building the skybox fragment shader module");

        VkShaderModule skyboxVertexShader;
        if (!vkutil::load_shader_module("../../../shaders/skybox.vert.spv", _device, &skyboxVertexShader))
            ENGINE_LOG_ERROR("Error when building the skybox vertex shader module");

        VkPushConstantRange bufferRange{};
        bufferRange.offset = 0;
        bufferRange.size = sizeof(glm::mat4);
        bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineLayoutCreateInfo pipeline_layout_info =
            vkinit::pipeline_layout_create_info();

        pipeline_layout_info.pPushConstantRanges = &bufferRange;
        pipeline_layout_info.pushConstantRangeCount = 1;
        pipeline_layout_info.pSetLayouts = &_skyboxDescriptorLayout;
        pipeline_layout_info.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(
            _device,
            &pipeline_layout_info,
            nullptr,
            &_skyboxPipelineLayout
        ));

        PipelineBuilder pipelineBuilder;
        pipelineBuilder._pipelineLayout = _skyboxPipelineLayout;

        pipelineBuilder.set_shaders(skyboxVertexShader, skyboxFragShader);
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipelineBuilder.set_multisampling_none();
        pipelineBuilder.disable_blending();

        // Skybox: depth test on, but depth write should ideally be off.
        pipelineBuilder.enable_depthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

        pipelineBuilder.set_color_attachment_format(_drawImage.imageFormat);
        pipelineBuilder.set_depth_format(_depthImage.imageFormat);

        _skyboxPipeline = pipelineBuilder.build_pipeline(_device);

        vkDestroyShaderModule(_device, skyboxFragShader, nullptr);
        vkDestroyShaderModule(_device, skyboxVertexShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _skyboxPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _skyboxPipeline, nullptr);
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

        VkDescriptorSetLayout setLayouts[] = {
            _multiImageDescriptorLayout,      // set 0
            _gpuSceneDataDescriptorLayout,    // set 1
            _environmentDescriptorLayout      // set 2
        };

        VkPipelineLayoutCreateInfo pipeline_layout_info = vkinit::pipeline_layout_create_info();
        pipeline_layout_info.pPushConstantRanges = &bufferRange;
        pipeline_layout_info.pushConstantRangeCount = 1;
	    pipeline_layout_info.pSetLayouts = setLayouts;
        pipeline_layout_info.setLayoutCount = 3;

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

        uint32_t black = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1));
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
        _errorCheckerboardImage = CreateImage(
            pixels.data(),
            VkExtent3D{16, 16, 1},
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        );

        _brdfLUTImage = CreateImage(
            nullptr,
            VkExtent3D{512, 512, 1},
            VK_FORMAT_R16G16_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT
        );

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

            vkDestroySampler(_device,_skyboxCubemap.sampler,nullptr);
            vkDestroySampler(_device,_brdfLUTSampler,nullptr);
            vkDestroySampler(_device,_prefilteredCubemap.sampler,nullptr);

            DestroyImage(_whiteImage);
            DestroyImage(_greyImage);
            DestroyImage(_blackImage);
            DestroyImage(_errorCheckerboardImage);
            DestroyImage(_brdfLUTImage);
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

        // Load Cubemap images
        _skyboxCubemap.image = CreateCubemap({
            "../../../assets/right.png",
            "../../../assets/left.png",
            "../../../assets/top.png",
            "../../../assets/bottom.png",
            "../../../assets/front.png",
            "../../../assets/back.png"
        });

        std::cout << "Cubemap mip levels: "
          << _skyboxCubemap.image.mipLevels
          << "\n";

        // Prefilter cubemap
        uint32_t prefilterSize = 128;
        uint32_t prefilterMipLevels = 5;

        _prefilteredCubemap.image =
            CreateEmptyCubemap(
                prefilterSize,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                prefilterMipLevels
            );

        _prefilteredCubemap.sampler = _skyboxCubemap.sampler;

        _mainDeletionQueue.push_function([&]() {
            DestroyImage(_prefilteredCubemap.image);
        });

        _irradianceCubemap.image =
            CreateEmptyCubemap(
                32,
                VK_FORMAT_R16G16B16A16_SFLOAT,
                1
            );

        _mainDeletionQueue.push_function([&]() {
            DestroyImage(_irradianceCubemap.image);
        });

        VkSamplerCreateInfo cubeSamplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
        };

        cubeSamplerInfo.magFilter = VK_FILTER_LINEAR;
        cubeSamplerInfo.minFilter = VK_FILTER_LINEAR;
        cubeSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        cubeSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cubeSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        cubeSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        cubeSamplerInfo.minLod = 0.0f;
        cubeSamplerInfo.maxLod =
            static_cast<float>(_skyboxCubemap.image.mipLevels);

        cubeSamplerInfo.mipLodBias = 0.0f;

        vkCreateSampler(_device, &cubeSamplerInfo, nullptr, &_skyboxCubemap.sampler);

        VkSamplerCreateInfo brdfSamplerInfo{ .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        brdfSamplerInfo.magFilter = VK_FILTER_LINEAR;
        brdfSamplerInfo.minFilter = VK_FILTER_LINEAR;
        brdfSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        brdfSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        brdfSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        brdfSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        brdfSamplerInfo.minLod = 0.0f;
        brdfSamplerInfo.maxLod = 1.0f;

        vkCreateSampler(_device, &brdfSamplerInfo, nullptr, &_brdfLUTSampler);

        // prefiltered sampler
        VkSamplerCreateInfo prefilterSamplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO
        };

        prefilterSamplerInfo.magFilter = VK_FILTER_LINEAR;
        prefilterSamplerInfo.minFilter = VK_FILTER_LINEAR;
        prefilterSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        prefilterSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        prefilterSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        prefilterSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        prefilterSamplerInfo.minLod = 0.0f;
        prefilterSamplerInfo.maxLod =
            static_cast<float>(_prefilteredCubemap.image.mipLevels - 1);

        prefilterSamplerInfo.mipLodBias = 0.0f;

        VK_CHECK(vkCreateSampler(
            _device,
            &prefilterSamplerInfo,
            nullptr,
            &_prefilteredCubemap.sampler
        ));

        //_skyboxCubemap.sampler = _defaultSamplerLinear;

        _skyboxCubemap.descriptorSet =
            globalDescriptorAllocator.Allocate(
                _device,
                _skyboxDescriptorLayout
            );

        DescriptorWriter skyboxWriter;
        skyboxWriter.write_image(
            0,
            _skyboxCubemap.image.imageView,
            _skyboxCubemap.sampler,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        skyboxWriter.update_set(_device, _skyboxCubemap.descriptorSet);

        _mainDeletionQueue.push_function([&]() {
            DestroyImage(_skyboxCubemap.image);
        });

        // environment
        _environmentDescriptorSet =
        globalDescriptorAllocator.Allocate(
            _device,
            _environmentDescriptorLayout
        );

        _irradianceCubemap.sampler = _skyboxCubemap.sampler;
        GenerateBRDFLUT();
        GeneratePrefilteredCubemap();
        GenerateIrradianceCubemap();
        DescriptorWriter envWriter;

        envWriter.write_image(
            0,
            _irradianceCubemap.image.imageView,
            _irradianceCubemap.sampler,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        envWriter.write_image(
            1,
            _prefilteredCubemap.image.imageView,
            _prefilteredCubemap.sampler,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        //   envWriter.write_image(1, _skyboxCubemap.image.imageView, _skyboxCubemap.sampler,
        //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        //     VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

        envWriter.write_image(
            2,
            _brdfLUTImage.imageView,
            _brdfLUTSampler,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        envWriter.update_set(_device, _environmentDescriptorSet);
    }

    void Core::InitPrefilterPipeline()
    {
        VkShaderModule fragShader;
        if (!vkutil::load_shader_module("../../../shaders/prefilter.frag.spv", _device, &fragShader)) {
            ENGINE_LOG_ERROR("Error when building prefilter fragment shader module");
        }

        VkShaderModule vertShader;
        if (!vkutil::load_shader_module("../../../shaders/prefilter.vert.spv", _device, &vertShader)) {
            ENGINE_LOG_ERROR("Error when building prefilter vertex shader module");
        }

        VkPushConstantRange pushRange{};
        pushRange.offset = 0;
        pushRange.size = sizeof(PrefilterPushConstants);
        pushRange.stageFlags =
            VK_SHADER_STAGE_VERTEX_BIT |
            VK_SHADER_STAGE_FRAGMENT_BIT;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo =
            vkinit::pipeline_layout_create_info();

        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pSetLayouts = &_skyboxDescriptorLayout;
        pipelineLayoutInfo.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(
            _device,
            &pipelineLayoutInfo,
            nullptr,
            &_prefilterPipelineLayout
        ));

        PipelineBuilder pipelineBuilder;

        pipelineBuilder._pipelineLayout = _prefilterPipelineLayout;

        pipelineBuilder.set_shaders(vertShader, fragShader);
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipelineBuilder.set_multisampling_none();
        pipelineBuilder.disable_blending();

        pipelineBuilder.disable_depthtest();

        pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);

        _prefilterPipeline =
            pipelineBuilder.build_pipeline(_device);

        vkDestroyShaderModule(_device, fragShader, nullptr);
        vkDestroyShaderModule(_device, vertShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _prefilterPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _prefilterPipeline, nullptr);
        });
    }

    void Core::InitIrradiancePipeline()
    {
        VkShaderModule fragShader;
        vkutil::load_shader_module("../../../shaders/irradiance.frag.spv", _device, &fragShader);

        VkShaderModule vertShader;
        vkutil::load_shader_module("../../../shaders/prefilter.vert.spv", _device, &vertShader);

        VkPushConstantRange pushRange{};
        pushRange.offset = 0;
        pushRange.size = sizeof(PrefilterPushConstants);
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineLayoutCreateInfo pipelineLayoutInfo =
            vkinit::pipeline_layout_create_info();

        pipelineLayoutInfo.pPushConstantRanges = &pushRange;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pSetLayouts = &_skyboxDescriptorLayout;
        pipelineLayoutInfo.setLayoutCount = 1;

        VK_CHECK(vkCreatePipelineLayout(
            _device,
            &pipelineLayoutInfo,
            nullptr,
            &_irradiancePipelineLayout
        ));

        PipelineBuilder pipelineBuilder;
        pipelineBuilder._pipelineLayout = _irradiancePipelineLayout;

        pipelineBuilder.set_shaders(vertShader, fragShader);
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipelineBuilder.set_multisampling_none();
        pipelineBuilder.disable_blending();
        pipelineBuilder.disable_depthtest();
        pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16B16A16_SFLOAT);

        _irradiancePipeline = pipelineBuilder.build_pipeline(_device);

        vkDestroyShaderModule(_device, fragShader, nullptr);
        vkDestroyShaderModule(_device, vertShader, nullptr);
    }

    void Core::InitBRDFLUTPipeline()
    {
         VkShaderModule fragShader;
        if (!vkutil::load_shader_module("../../../shaders/brdf_lut.frag.spv", _device, &fragShader)) {
            ENGINE_LOG_ERROR("Error when building BRDF LUT fragment shader module");
        }

        VkShaderModule vertShader;
        if (!vkutil::load_shader_module("../../../shaders/fullscreen_triangle.vert.spv", _device, &vertShader)) {
            ENGINE_LOG_ERROR("Error when building fullscreen triangle vertex shader module");
        }

        VkPipelineLayoutCreateInfo pipelineLayoutInfo =
            vkinit::pipeline_layout_create_info();

        pipelineLayoutInfo.pPushConstantRanges = nullptr;
        pipelineLayoutInfo.pushConstantRangeCount = 0;
        pipelineLayoutInfo.pSetLayouts = nullptr;
        pipelineLayoutInfo.setLayoutCount = 0;

        VK_CHECK(vkCreatePipelineLayout(
            _device,
            &pipelineLayoutInfo,
            nullptr,
            &_brdfLUTPipelineLayout
        ));

        PipelineBuilder pipelineBuilder;

        pipelineBuilder._pipelineLayout = _brdfLUTPipelineLayout;

        pipelineBuilder.set_shaders(vertShader, fragShader);
        pipelineBuilder.set_input_topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        pipelineBuilder.set_polygon_mode(VK_POLYGON_MODE_FILL);
        pipelineBuilder.set_cull_mode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
        pipelineBuilder.set_multisampling_none();
        pipelineBuilder.disable_blending();

        pipelineBuilder.disable_depthtest();

        pipelineBuilder.set_color_attachment_format(VK_FORMAT_R16G16_SFLOAT);

        _brdfLUTPipeline =
            pipelineBuilder.build_pipeline(_device);

        vkDestroyShaderModule(_device, fragShader, nullptr);
        vkDestroyShaderModule(_device, vertShader, nullptr);

        _mainDeletionQueue.push_function([&]() {
            vkDestroyPipelineLayout(_device, _brdfLUTPipelineLayout, nullptr);
            vkDestroyPipeline(_device, _brdfLUTPipeline, nullptr);
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

        auto parentPath = filePath.parent_path();
        auto extension = filePath.extension().string();

        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char c) { return std::tolower(c); });

        if (extension == ".glb") {
            auto load = parser.loadGltfBinary(data, parentPath, gltfOptions);

            if (!load) {
                return std::nullopt;
            }

            gltf = std::move(load.get());
        }
        else if (extension == ".gltf") {
            auto load = parser.loadGltf(data, parentPath, gltfOptions);

            if (!load) {
                return std::nullopt;
            }

            gltf = std::move(load.get());
        }
        else {
            return std::nullopt;
        }
        // Load images
        _loadedImages.clear();
        _loadedImages.resize(gltf.images.size());

        std::cout << "Loading glTF images: " << gltf.images.size() << "\n";

        for (size_t i = 0; i < gltf.images.size(); i++) {
            auto loadedImage = LoadGltfImage(gltf, gltf.images[i]);

            if (loadedImage.has_value()) {
                _loadedImages[i] =
                    std::make_shared<AllocatedImage>(loadedImage.value());

                AllocatedImage* imageToDelete = _loadedImages[i].get();

                _mainDeletionQueue.push_function([this, imageToDelete]() {
                    DestroyImage(*imageToDelete);
                });

                std::cout << "Loaded image " << i << "\n";
            }
        }
        
        // end of load images

        // -------------------------
        // LOAD MATERIALS
        // -------------------------

        _loadedMaterials.clear();
        _loadedMaterials.resize(gltf.materials.size());

        for (size_t i = 0; i < gltf.materials.size(); i++) {

            auto& gltfMaterial = gltf.materials[i];
            auto& material = _loadedMaterials[i];

            // -------------------------
            // DEFAULTS
            // -------------------------

            material.image = &_errorCheckerboardImage;

            material.normalImage = &_errorCheckerboardImage;

            material.metallicRoughnessImage = &_whiteImage;

            material.occlusionImage = &_whiteImage;

            material.emissionImage = &_blackImage;

            // -------------------------
            // BASE COLOR
            // -------------------------

            if (gltfMaterial.pbrData.baseColorTexture.has_value()) {

                auto textureIndex =
                    gltfMaterial.pbrData.baseColorTexture.value().textureIndex;

                auto& texture = gltf.textures[textureIndex];

                if (texture.imageIndex.has_value()) {

                    auto imageIndex = texture.imageIndex.value();

                    if (imageIndex < _loadedImages.size()) {

                        material.image =
                            _loadedImages[imageIndex].get();
                    }
                }
            }

            // -------------------------
            // NORMAL
            // -------------------------

            if (gltfMaterial.normalTexture.has_value()) {

                auto textureIndex =
                    gltfMaterial.normalTexture.value().textureIndex;

                auto& texture = gltf.textures[textureIndex];

                if (texture.imageIndex.has_value()) {

                    auto imageIndex = texture.imageIndex.value();

                    if (imageIndex < _loadedImages.size()) {

                        material.normalImage =
                            _loadedImages[imageIndex].get();
                    }
                }
            }

            // -------------------------
            // METALLIC ROUGHNESS
            // -------------------------

            if (gltfMaterial.pbrData.metallicRoughnessTexture.has_value()) {

                auto textureIndex =
                    gltfMaterial.pbrData
                        .metallicRoughnessTexture.value()
                        .textureIndex;

                auto& texture = gltf.textures[textureIndex];

                if (texture.imageIndex.has_value()) {

                    auto imageIndex = texture.imageIndex.value();

                    if (imageIndex < _loadedImages.size()) {

                        material.metallicRoughnessImage =
                            _loadedImages[imageIndex].get();
                    }
                }
            }

            // -------------------------
            // OCCLUSION
            // -------------------------

            if (gltfMaterial.occlusionTexture.has_value()) {

                auto textureIndex =
                    gltfMaterial.occlusionTexture.value().textureIndex;

                auto& texture = gltf.textures[textureIndex];

                if (texture.imageIndex.has_value()) {

                    auto imageIndex = texture.imageIndex.value();

                    if (imageIndex < _loadedImages.size()) {

                        material.occlusionImage =
                            _loadedImages[imageIndex].get();
                    }
                }
            }

            // -------------------------
            // EMISSIVE
            // -------------------------

            if (gltfMaterial.emissiveTexture.has_value()) {

                auto textureIndex =
                    gltfMaterial.emissiveTexture.value().textureIndex;

                auto& texture = gltf.textures[textureIndex];

                if (texture.imageIndex.has_value()) {

                    auto imageIndex = texture.imageIndex.value();

                    if (imageIndex < _loadedImages.size()) {

                        material.emissionImage =
                            _loadedImages[imageIndex].get();
                    }
                }
            }
        }

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
                bool hasTangents = false;
                bool hasNormals = false;
                bool hasUVs = false;
                newSurface.startIndex = static_cast<uint32_t>(indices.size());
                newSurface.material = nullptr;

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
                        // test tangent
                        //v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
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

                    if (normalAccessor.type == fastgltf::AccessorType::Vec3 &&
                        normalAccessor.count == positionAccessor.count) {

                        fastgltf::iterateAccessorWithIndex<glm::vec3>(
                            gltf,
                            normalAccessor,
                            [&](glm::vec3 normal, size_t index) {
                                vertices[initial_vtx + index].normal = normal;
                            });
                        hasNormals = true;
                    }
                }

                // -------------------------
                // UVs (optional)
                // -------------------------
                if (auto* uvsAttr = p.findAttribute("TEXCOORD_0")) {
                    auto& uvAccessor = gltf.accessors[uvsAttr->accessorIndex];

                    if (uvAccessor.type == fastgltf::AccessorType::Vec2 &&
                        uvAccessor.count == positionAccessor.count) {

                        fastgltf::iterateAccessorWithIndex<glm::vec2>(
                            gltf,
                            uvAccessor,
                            [&](glm::vec2 uv, size_t index) {
                                vertices[initial_vtx + index].uv_x = uv.x;
                                vertices[initial_vtx + index].uv_y = uv.y;
                            });
                        hasUVs = true;
                    }
                }
                // -------------------------
                // Tangents (optional)
                // -------------------------
                if (auto* tangentAttr = p.findAttribute("TANGENT")) {
                    auto& tangentAccessor = gltf.accessors[tangentAttr->accessorIndex];

                    if (tangentAccessor.type == fastgltf::AccessorType::Vec4 &&
                        tangentAccessor.count == positionAccessor.count) {

                        fastgltf::iterateAccessorWithIndex<glm::vec4>(
                            gltf,
                            tangentAccessor,
                            [&](glm::vec4 tangent, size_t index) {
                                vertices[initial_vtx + index].tangent = tangent;
                            });
                        hasTangents = true;
                    }
                }

                if (p.materialIndex.has_value()) {

                    uint32_t materialIndex = p.materialIndex.value();

                    if (materialIndex < _loadedMaterials.size()) {
                        newSurface.material =
                            &_loadedMaterials[materialIndex];
                    }
                }
                if (!hasTangents && hasNormals && hasUVs) {
                    GenerateTangents(
                        vertices,
                        indices,
                        newSurface.startIndex,
                        newSurface.count
                    );
                }

                newMesh.surfaces.push_back(newSurface);
            }

            // Prevent uploading empty meshes
            if (!indices.empty() && !vertices.empty()) {
                newMesh.meshBuffers = engine->UploadMesh(indices, vertices);

                // push into deletion queue
                auto vertexBuffer = newMesh.meshBuffers.vertexBuffer;
                auto indexBuffer = newMesh.meshBuffers.indexBuffer;

                _mainDeletionQueue.push_function([this, vertexBuffer, indexBuffer]() {
                    DestroyBuffer(vertexBuffer);
                    DestroyBuffer(indexBuffer);
                });
                // push for return
                meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
            }
        }

        return meshes;
    }

    // std::optional<AllocatedImage> Core::LoadGltfImage(fastgltf::Asset &asset, fastgltf::Image &image, const std::filesystem::path &parentPath)
    // {
    //     return std::optional<AllocatedImage>();
    // }

    std::optional<AllocatedImage> Core::LoadGltfImage(fastgltf::Asset &asset, fastgltf::Image &image)
    {
       AllocatedImage newImage{};

        int width = 0;
        int height = 0;
        int channels = 0;

        std::visit(
            fastgltf::visitor{
                [](std::monostate&) {},

                [&](fastgltf::sources::URI& filePath) {
                    if (filePath.fileByteOffset != 0) {
                        std::cout << "Unsupported image URI byte offset\n";
                        return;
                    }

                    if (!filePath.uri.isLocalPath()) {
                        std::cout << "Unsupported non-local image URI\n";
                        return;
                    }

                    std::string path(
                        filePath.uri.path().begin(),
                        filePath.uri.path().end()
                    );

                    unsigned char* data = stbi_load(
                        path.c_str(),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    if (!data) {
                        std::cout << "Failed to load image: " << path << "\n";
                        return;
                    }

                    newImage = CreateImage(
                        data,
                        VkExtent3D{
                            static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height),
                            1
                        },
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    );

                    stbi_image_free(data);
                },

                [&](fastgltf::sources::Array& array) {
                    unsigned char* data = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(array.bytes.data()),
                        static_cast<int>(array.bytes.size()),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    if (!data) {
                        std::cout << "Failed to load image from Array\n";
                        return;
                    }

                    newImage = CreateImage(
                        data,
                        VkExtent3D{
                            static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height),
                            1
                        },
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    );

                    stbi_image_free(data);
                },

                [&](fastgltf::sources::Vector& vector) {
                    unsigned char* data = stbi_load_from_memory(
                        reinterpret_cast<const stbi_uc*>(vector.bytes.data()),
                        static_cast<int>(vector.bytes.size()),
                        &width,
                        &height,
                        &channels,
                        STBI_rgb_alpha
                    );

                    if (!data) {
                        std::cout << "Failed to load image from Vector\n";
                        return;
                    }

                    newImage = CreateImage(
                        data,
                        VkExtent3D{
                            static_cast<uint32_t>(width),
                            static_cast<uint32_t>(height),
                            1
                        },
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                    );

                    stbi_image_free(data);
                },

                [&](fastgltf::sources::BufferView& view) {
                    auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                    auto& buffer = asset.buffers[bufferView.bufferIndex];

                    std::visit(
                        fastgltf::visitor{
                            [](auto&) {},

                            [&](fastgltf::sources::Vector& vector) {
                                unsigned char* data = stbi_load_from_memory(
                                    reinterpret_cast<const stbi_uc*>(vector.bytes.data()) + bufferView.byteOffset,
                                    static_cast<int>(bufferView.byteLength),
                                    &width,
                                    &height,
                                    &channels,
                                    STBI_rgb_alpha
                                );

                                if (!data) {
                                    std::cout << "Failed to load image from BufferView\n";
                                    return;
                                }

                                newImage = CreateImage(
                                    data,
                                    VkExtent3D{
                                        static_cast<uint32_t>(width),
                                        static_cast<uint32_t>(height),
                                        1
                                    },
                                    VK_FORMAT_R8G8B8A8_UNORM,
                                    VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                                );

                                stbi_image_free(data);
                            }
                        },
                        buffer.data
                    );
                },

                [](auto&) {}
            },
            image.data
        );

        if (newImage.image == VK_NULL_HANDLE) {
            return {};
        }

        newImage.sampler = _defaultSamplerLinear;

        newImage.imguiDescriptorSet = ImGui_ImplVulkan_AddTexture(
            newImage.sampler,
            newImage.imageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        return newImage;
    }

    AllocatedImage Core::CreateCubemap(const std::array<std::string, 6>& facePaths)
    {
        int width = 0;
        int height = 0;
        int channels = 0;

        std::array<stbi_uc*, 6> faces{};

        for (int i = 0; i < 6; i++) {
            faces[i] = stbi_load(
                facePaths[i].c_str(),
                &width,
                &height,
                &channels,
                STBI_rgb_alpha
            );

            if (!faces[i]) {
                std::cout << "Failed to load cubemap face: "
                        << facePaths[i] << "\n";
            }
        }

        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

        VkDeviceSize faceSize = width * height * 4;
        VkDeviceSize totalSize = faceSize * 6;

        AllocatedBuffer stagingBuffer = CreateBuffer(
            totalSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU
        );

        void* mappedData = stagingBuffer.allocation->GetMappedData();

        for (int i = 0; i < 6; i++) {
            memcpy(
                static_cast<char*>(mappedData) + faceSize * i,
                faces[i],
                faceSize
            );
        }

        for (int i = 0; i < 6; i++) {
            stbi_image_free(faces[i]);
        }

        AllocatedImage cubemap{};
        cubemap.mipLevels = mipLevels;

        cubemap.imageExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            1
        };

        cubemap.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = cubemap.imageFormat;
        imageInfo.extent = cubemap.imageExtent;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 6;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VK_CHECK(vmaCreateImage(
            _allocator,
            &imageInfo,
            &allocInfo,
            &cubemap.image,
            &cubemap.allocation,
            nullptr
        ));

        ImmediateSubmit([&](VkCommandBuffer cmd) {
            VkImageSubresourceRange range{};
            range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel = 0;
            range.levelCount = mipLevels;
            range.baseArrayLayer = 0;
            range.layerCount = 6;

            vkutil::transition_image(
                cmd,
                cubemap.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                range
            );

            std::array<VkBufferImageCopy, 6> copyRegions{};

            for (uint32_t i = 0; i < 6; i++) {
                copyRegions[i].bufferOffset = faceSize * i;
                copyRegions[i].bufferRowLength = 0;
                copyRegions[i].bufferImageHeight = 0;

                copyRegions[i].imageSubresource.aspectMask =
                    VK_IMAGE_ASPECT_COLOR_BIT;
                copyRegions[i].imageSubresource.mipLevel = 0;
                copyRegions[i].imageSubresource.baseArrayLayer = i;
                copyRegions[i].imageSubresource.layerCount = 1;

                copyRegions[i].imageExtent = cubemap.imageExtent;
            }

            vkCmdCopyBufferToImage(
                cmd,
                stagingBuffer.buffer,
                cubemap.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                static_cast<uint32_t>(copyRegions.size()),
                copyRegions.data()
            );
            GenerateCubemapMipmaps(
                cmd,
                cubemap.image,
                cubemap.imageExtent.width,
                cubemap.imageExtent.height,
                mipLevels
            );

            // vkutil::transition_image(
            //     cmd,
            //     cubemap.image,
            //     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            //     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            //     range
            // );
        });

        DestroyBuffer(stagingBuffer);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cubemap.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = cubemap.imageFormat;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;

        VK_CHECK(vkCreateImageView(
            _device,
            &viewInfo,
            nullptr,
            &cubemap.imageView
        ));
        return cubemap;
    }

    AllocatedImage Core::CreateEmptyCubemap(uint32_t size, VkFormat format, uint32_t mipLevels)
    {
        AllocatedImage cubemap{};

        cubemap.imageExtent = VkExtent3D{ size, size, 1 };
        cubemap.imageFormat = format;
        cubemap.mipLevels = mipLevels;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = cubemap.imageExtent;
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 6;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage =
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VK_CHECK(vmaCreateImage(
            _allocator,
            &imageInfo,
            &allocInfo,
            &cubemap.image,
            &cubemap.allocation,
            nullptr
        ));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = cubemap.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        viewInfo.format = format;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 6;

        VK_CHECK(vkCreateImageView(
            _device,
            &viewInfo,
            nullptr,
            &cubemap.imageView
        ));

        return cubemap;
    }

    void Core::GenerateCubemapMipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels)
    {
         int32_t mipWidth = static_cast<int32_t>(width);
        int32_t mipHeight = static_cast<int32_t>(height);

        for (uint32_t i = 1; i < mipLevels; i++) {

            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.image = image;

            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 6;

            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;

            vkCmdPipelineBarrier2(cmd, &depInfo);

            for (uint32_t face = 0; face < 6; face++) {
                VkImageBlit2 blit{};
                blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;

                blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.srcSubresource.mipLevel = i - 1;
                blit.srcSubresource.baseArrayLayer = face;
                blit.srcSubresource.layerCount = 1;

                blit.srcOffsets[0] = {0, 0, 0};
                blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

                blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                blit.dstSubresource.mipLevel = i;
                blit.dstSubresource.baseArrayLayer = face;
                blit.dstSubresource.layerCount = 1;

                blit.dstOffsets[0] = {0, 0, 0};
                blit.dstOffsets[1] = {
                    mipWidth > 1 ? mipWidth / 2 : 1,
                    mipHeight > 1 ? mipHeight / 2 : 1,
                    1
                };

                VkBlitImageInfo2 blitInfo{};
                blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
                blitInfo.srcImage = image;
                blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                blitInfo.dstImage = image;
                blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                blitInfo.regionCount = 1;
                blitInfo.pRegions = &blit;
                blitInfo.filter = VK_FILTER_LINEAR;

                vkCmdBlitImage2(cmd, &blitInfo);
            }

            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            vkCmdPipelineBarrier2(cmd, &depInfo);

            mipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            mipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = image;

        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 6;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);
    }

    void Core::GenerateTangents(std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices, uint32_t startIndex, uint32_t indexCount)
    {
        std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
        std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

        for (uint32_t i = startIndex; i + 2 < startIndex + indexCount; i += 3) {
            uint32_t i0 = indices[i + 0];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            Vertex& v0 = vertices[i0];
            Vertex& v1 = vertices[i1];
            Vertex& v2 = vertices[i2];

            glm::vec3 p0 = v0.position;
            glm::vec3 p1 = v1.position;
            glm::vec3 p2 = v2.position;

            glm::vec2 uv0(v0.uv_x, v0.uv_y);
            glm::vec2 uv1(v1.uv_x, v1.uv_y);
            glm::vec2 uv2(v2.uv_x, v2.uv_y);

            glm::vec3 e1 = p1 - p0;
            glm::vec3 e2 = p2 - p0;

            glm::vec2 duv1 = uv1 - uv0;
            glm::vec2 duv2 = uv2 - uv0;

            float denom = duv1.x * duv2.y - duv2.x * duv1.y;
            if (std::abs(denom) < 1e-8f)
                continue;

            float r = 1.0f / denom;

            glm::vec3 tangent =
                (e1 * duv2.y - e2 * duv1.y) * r;

            glm::vec3 bitangent =
                (e2 * duv1.x - e1 * duv2.x) * r;

            tan1[i0] += tangent;
            tan1[i1] += tangent;
            tan1[i2] += tangent;

            tan2[i0] += bitangent;
            tan2[i1] += bitangent;
            tan2[i2] += bitangent;
        }

        for (uint32_t i = startIndex; i < startIndex + indexCount; i++) {
            uint32_t vi = indices[i];

            glm::vec3 n = glm::normalize(vertices[vi].normal);
            glm::vec3 t = tan1[vi];

            // Gram-Schmidt orthogonalize
            t = glm::normalize(t - n * glm::dot(n, t));

            float handedness =
                glm::dot(glm::cross(n, t), tan2[vi]) < 0.0f ? -1.0f : 1.0f;

            vertices[vi].tangent = glm::vec4(t, handedness);
        }
    }

    void Core::GenerateBRDFLUT()
    {
        ImmediateSubmit([&](VkCommandBuffer cmd) {
        vkutil::transition_image(
            cmd,
            _brdfLUTImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        );

        VkRenderingAttachmentInfo colorAttachment =
            vkinit::attachment_info(
                _brdfLUTImage.imageView,
                nullptr,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            );

        VkRenderingInfo renderInfo =
            vkinit::rendering_info(
                VkExtent2D{512, 512},
                &colorAttachment,
                nullptr
            );

        vkCmdBeginRendering(cmd, &renderInfo);

        VkViewport viewport{};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = 512.0f;
        viewport.height = 512.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = {512, 512};

        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _brdfLUTPipeline
        );

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        vkutil::transition_image(
            cmd,
            _brdfLUTImage.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    });
    }

    void Core::GeneratePrefilteredCubemap()
    {
        ImmediateSubmit([&](VkCommandBuffer cmd) {

            VkImageSubresourceRange fullRange{};
            fullRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            fullRange.baseMipLevel = 0;
            fullRange.levelCount = _prefilteredCubemap.image.mipLevels;
            fullRange.baseArrayLayer = 0;
            fullRange.layerCount = 6;

            vkutil::transition_image(
                cmd,
                _prefilteredCubemap.image.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                fullRange
            );

            glm::mat4 captureProjection =
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

            std::array<glm::mat4, 6> captureViews = {
                glm::lookAt(glm::vec3(0), glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0))
            };

            for (uint32_t mip = 0; mip < _prefilteredCubemap.image.mipLevels; mip++) {

                uint32_t mipSize =
                    static_cast<uint32_t>(
                        _prefilteredCubemap.image.imageExtent.width *
                        std::pow(0.5f, mip)
                    );

                float roughness =
                    static_cast<float>(mip) /
                    static_cast<float>(_prefilteredCubemap.image.mipLevels - 1);

                for (uint32_t face = 0; face < 6; face++) {

                    VkImageViewCreateInfo faceViewInfo{};
                    faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                    faceViewInfo.image = _prefilteredCubemap.image.image;
                    faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                    faceViewInfo.format = _prefilteredCubemap.image.imageFormat;

                    faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    faceViewInfo.subresourceRange.baseMipLevel = mip;
                    faceViewInfo.subresourceRange.levelCount = 1;
                    faceViewInfo.subresourceRange.baseArrayLayer = face;
                    faceViewInfo.subresourceRange.layerCount = 1;

                    VkImageView faceView;
                    VK_CHECK(vkCreateImageView(
                        _device,
                        &faceViewInfo,
                        nullptr,
                        &faceView
                    ));

                    VkRenderingAttachmentInfo colorAttachment =
                        vkinit::attachment_info(
                            faceView,
                            nullptr,
                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                        );

                    VkRenderingInfo renderInfo =
                        vkinit::rendering_info(
                            VkExtent2D{ mipSize, mipSize },
                            &colorAttachment,
                            nullptr
                        );

                    vkCmdBeginRendering(cmd, &renderInfo);

                    VkViewport viewport{};
                    viewport.x = 0.0f;
                    viewport.y = 0.0f;
                    viewport.width = static_cast<float>(mipSize);
                    viewport.height = static_cast<float>(mipSize);
                    viewport.minDepth = 0.0f;
                    viewport.maxDepth = 1.0f;

                    vkCmdSetViewport(cmd, 0, 1, &viewport);

                    VkRect2D scissor{};
                    scissor.offset = {0, 0};
                    scissor.extent = {mipSize, mipSize};

                    vkCmdSetScissor(cmd, 0, 1, &scissor);

                    vkCmdBindPipeline(
                        cmd,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        _prefilterPipeline
                    );

                    vkCmdBindDescriptorSets(
                        cmd,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        _prefilterPipelineLayout,
                        0,
                        1,
                        &_skyboxCubemap.descriptorSet,
                        0,
                        nullptr
                    );

                    PrefilterPushConstants pc{};
                    pc.viewProjection =
                        captureProjection * captureViews[face];

                    pc.roughness = roughness;

                    vkCmdPushConstants(
                        cmd,
                        _prefilterPipelineLayout,
                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                        0,
                        sizeof(PrefilterPushConstants),
                        &pc
                    );

                    vkCmdDraw(cmd, 36, 1, 0, 0);

                    vkCmdEndRendering(cmd);

                    vkDestroyImageView(_device, faceView, nullptr);
                }
            }

            vkutil::transition_image(
                cmd,
                _prefilteredCubemap.image.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                fullRange
            );
        });
    }

    void Core::GenerateIrradianceCubemap()
    {
        ImmediateSubmit([&](VkCommandBuffer cmd) {

            VkImageSubresourceRange fullRange{};
            fullRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            fullRange.baseMipLevel = 0;
            fullRange.levelCount = 1;
            fullRange.baseArrayLayer = 0;
            fullRange.layerCount = 6;

            vkutil::transition_image(
                cmd,
                _irradianceCubemap.image.image,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                fullRange
            );

            glm::mat4 captureProjection =
                glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);

            std::array<glm::mat4, 6> captureViews = {
                glm::lookAt(glm::vec3(0), glm::vec3( 1,  0,  0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3(-1,  0,  0), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  1,  0), glm::vec3(0,  0,  1)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0, -1,  0), glm::vec3(0,  0, -1)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  0,  1), glm::vec3(0, -1,  0)),
                glm::lookAt(glm::vec3(0), glm::vec3( 0,  0, -1), glm::vec3(0, -1,  0))
            };

            uint32_t size = _irradianceCubemap.image.imageExtent.width;

            for (uint32_t face = 0; face < 6; face++) {

                VkImageViewCreateInfo faceViewInfo{};
                faceViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                faceViewInfo.image = _irradianceCubemap.image.image;
                faceViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
                faceViewInfo.format = _irradianceCubemap.image.imageFormat;

                faceViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                faceViewInfo.subresourceRange.baseMipLevel = 0;
                faceViewInfo.subresourceRange.levelCount = 1;
                faceViewInfo.subresourceRange.baseArrayLayer = face;
                faceViewInfo.subresourceRange.layerCount = 1;

                VkImageView faceView;
                VK_CHECK(vkCreateImageView(
                    _device,
                    &faceViewInfo,
                    nullptr,
                    &faceView
                ));

                VkRenderingAttachmentInfo colorAttachment =
                    vkinit::attachment_info(
                        faceView,
                        nullptr,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                    );

                VkRenderingInfo renderInfo =
                    vkinit::rendering_info(
                        VkExtent2D{size, size},
                        &colorAttachment,
                        nullptr
                    );

                vkCmdBeginRendering(cmd, &renderInfo);

                VkViewport viewport{};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(size);
                viewport.height = static_cast<float>(size);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;

                vkCmdSetViewport(cmd, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = {size, size};

                vkCmdSetScissor(cmd, 0, 1, &scissor);

                vkCmdBindPipeline(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _irradiancePipeline
                );

                vkCmdBindDescriptorSets(
                    cmd,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    _irradiancePipelineLayout,
                    0,
                    1,
                    &_skyboxCubemap.descriptorSet,
                    0,
                    nullptr
                );

                PrefilterPushConstants pc{};
                pc.viewProjection =
                    captureProjection * captureViews[face];

                pc.roughness = 0.0f;

                vkCmdPushConstants(
                    cmd,
                    _irradiancePipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT,
                    0,
                    sizeof(PrefilterPushConstants),
                    &pc
                );

                vkCmdDraw(cmd, 36, 1, 0, 0);

                vkCmdEndRendering(cmd);

                vkDestroyImageView(_device, faceView, nullptr);
            }

            vkutil::transition_image(
                cmd,
                _irradianceCubemap.image.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                fullRange
            );
        });
    }

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
        auto nameComponent = NameComponent("Camera");
        _registry.emplace<NameComponent>(cameraEntity, nameComponent);

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
            DrawUi();
            for (auto& system : _systems) {
                system->DrawUi();
            }
            if (ImGui::Begin("Texture Debugger")) {

            for (size_t i = 0; i < _loadedImages.size(); i++) {

                    auto& image = _loadedImages[i];

                    if (!image) {
                        ImGui::TextDisabled("Texture %zu : null", i);
                        continue;
                    }

                    ImGui::Text("Texture %zu", i);

                    ImTextureID id =
                        reinterpret_cast<ImTextureID>(image->imguiDescriptorSet);

                    if (!id) {
                        ImGui::TextDisabled("Missing ImGui descriptor");
                        continue;
                    }

                    ImGui::Image(id, ImVec2(128.0f, 128.0f));
                }
            }

            ImGui::End();

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

    // void GLTFMetallic_Roughness::BuildPipelines(Core *engine)
    // {
    // }

    // void GLTFMetallic_Roughness::ClearResources(VkDevice device)
    // {
    // }

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