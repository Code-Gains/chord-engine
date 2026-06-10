#include "Core.h"
#include "vk_initializers.h"

namespace Engine {

void Core::InitSwapchain() {
    CreateSwapchain(_window->GetWidth(), _window->GetHeight());
    CreateDrawImages(_window->GetWidth(), _window->GetHeight());
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

void Core::DestroySwapchain() {
    vkDestroySwapchainKHR(_device, _swapchain, nullptr);

    // destroy swapchain resources
    for (int i = 0; i < _swapchainImageViews.size(); i++) {
        vkDestroyImageView(_device, _swapchainImageViews[i], nullptr);
    }
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

void Core::CreateDrawImages(uint32_t width, uint32_t height)
{
    VkExtent3D drawImageExtent = { width, height, 1 };
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

    _msaaColorImage = CreateImage(
        drawImageExtent,
        _drawImage.imageFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        false,
        _msaaSamples
    );

    _msaaDepthImage = CreateImage(
        drawImageExtent,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        false,
        _msaaSamples
    );

    _depthImage = CreateImage(
        drawImageExtent,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        false,
        VK_SAMPLE_COUNT_1_BIT
    );

    _selectionMaskImage = CreateImage(
        drawImageExtent,
        VK_FORMAT_R8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        false,
        VK_SAMPLE_COUNT_1_BIT
    );
}

void Core::CleanupDrawImages()
{
    vkDestroyImageView(_device, _drawImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

    vkDestroyImageView(_device, _msaaColorImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _msaaColorImage.image, _msaaColorImage.allocation);

    vkDestroyImageView(_device, _msaaDepthImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _msaaDepthImage.image, _msaaDepthImage.allocation);

    vkDestroyImageView(_device, _depthImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);

    vkDestroyImageView(_device, _selectionMaskImage.imageView, nullptr);
    vmaDestroyImage(_allocator, _selectionMaskImage.image, _selectionMaskImage.allocation);
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
    vkDestroyDescriptorSetLayout(_device, _shadowDescriptorLayout , nullptr);
    vkDestroyDescriptorSetLayout(_device, _skyboxDescriptorLayout , nullptr);
}

void Core::InitSyncStructures() { // TODO potentially move out to CoreSync.cpp
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

} // end of namespace engine
