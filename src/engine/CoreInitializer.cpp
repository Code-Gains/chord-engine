#include "Core.h"
#include "EcsDebugger.h"
#include "Transform.h"
#include "Camera.h"
#include "CameraSystem.h"
#include "NameComponent.h"
#include "vk_initializers.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace Engine {
#ifndef NDEBUG
    constexpr bool gUseValidationLayers = true; // TODO move to global settings later
#else
    constexpr bool gUseValidationLayers = false; // TODO move to global settings later
#endif

void Core::InitWindow() {
    _window = std::make_unique<WindowGLFW>(1280, 720, "Chord Engine");
}

void Core::InitVulkan() {
    vkb::InstanceBuilder builder;
    // make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Chord Engine")
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

    uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
    _flatNormalImage = CreateImage((void*)&flatNormal, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t defaultMetallicRoughness = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
    _defaultMetallicRoughnessImage = CreateImage((void*)&defaultMetallicRoughness, VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
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
        DestroyImage(_flatNormalImage);
        DestroyImage(_defaultMetallicRoughnessImage);
        DestroyImage(_errorCheckerboardImage);
        DestroyImage(_brdfLUTImage);
    });



    //delete the rectangle data on engine shutdown
    _mainDeletionQueue.push_function([&](){
        DestroyBuffer(rectangle.indexBuffer);
        DestroyBuffer(rectangle.vertexBuffer);
    });

    _testMeshes = LoadEngineGltfMeshes(this,"assets/basicmesh.glb").value();
    auto tempMeshes = LoadEngineGltfMeshes(this,"assets/tetrahedron.glb").value();
    _testMeshes.insert(
        _testMeshes.end(),              // insert at the end of _testMeshes
        tempMeshes.begin(),             // start of tempMeshes
        tempMeshes.end()                // end of tempMeshes
    );

    // Load Cubemap images
    _skyboxCubemap.image = CreateCubemap({
        "assets/right.png",
        "assets/left.png",
        "assets/top.png",
        "assets/bottom.png",
        "assets/front.png",
        "assets/back.png"
    });

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

    envWriter.write_image(
        2,
        _brdfLUTImage.imageView,
        _brdfLUTSampler,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
    );

    envWriter.update_set(_device, _environmentDescriptorSet);
}
} // end of engine namespace
