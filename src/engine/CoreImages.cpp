#include "Core.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "stb_image.h"

namespace Engine {
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

        //memcpy(uploadBuffer.allocation->GetMappedData(), data, dataSize);
        memcpy(uploadBuffer.info.pMappedData, data, dataSize);
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

AllocatedImage Core::CreateCubemap(const std::array<std::string, 6>& facePaths)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    std::array<stbi_uc*, 6> faces{};

    for (int i = 0; i < 6; i++) {
        auto resolvedFacePath = ResolveEnginePath(facePaths[i]);

        faces[i] = stbi_load(
            resolvedFacePath.string().c_str(),
            &width,
            &height,
            &channels,
            STBI_rgb_alpha
        );

        if (!faces[i]) {
            std::cout << "Failed to load cubemap face: "
                    << resolvedFacePath << "\n";
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

    //void* mappedData = stagingBuffer.allocation->GetMappedData();
    void* mappedData = stagingBuffer.info.pMappedData;

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
}// end of namespace engine
