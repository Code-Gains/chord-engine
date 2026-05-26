#include "Core.h"
#include "vk_images.h"
#include "vk_initializers.h"

namespace Engine {
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
} // end of namespace engine