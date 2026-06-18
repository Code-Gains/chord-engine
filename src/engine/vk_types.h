// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.
//> intro
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <iostream>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
//< intro 



// we will add our main reusable types here
struct AllocatedImage {
    VkImage image;
    VkImageView imageView;
    VmaAllocation allocation;
    VkExtent3D imageExtent;
    VkFormat imageFormat;
    uint32_t mipLevels = 1;

    // debugging images
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet imguiDescriptorSet = VK_NULL_HANDLE;
};

struct AllocatedBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
    VmaAllocationInfo info;
    VkDeviceAddress deviceAddress = 0;
};

struct GPUGLTFMaterial {
    glm::vec4 colorFactors;
    glm::vec4 metal_rough_factors;
    glm::vec4 extra[14];
};

static_assert(sizeof(GPUGLTFMaterial) == 256);

struct GPUSceneData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 viewproj;
    glm::vec4 ambientColor;
    glm::vec4 sunlightDirection; // w for sun power
    glm::vec4 sunlightColor;
};

//> mat_types
enum class MaterialPass :uint8_t {
    MainColor,
    Transparent,
    Other
};
struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

struct MaterialInstance {
    MaterialPipeline* pipeline;
    VkDescriptorSet materialSet;
    MaterialPass passType;
    glm::vec4 baseColorFactor{ 1.0f };
    AllocatedImage* image = nullptr;
    AllocatedImage* metallicRoughnessImage = nullptr;
    AllocatedImage* normalImage = nullptr;
    AllocatedImage* occlusionImage = nullptr;
    AllocatedImage* emissionImage = nullptr;
};
//< mat_types
//> vbuf_types
struct Vertex {

	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
    glm::vec4 tangent;
	//glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {
    AllocatedBuffer indexBuffer;
    AllocatedBuffer vertexBuffer;
    VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
    glm::mat4 viewProjection;
    glm::mat4 model;
    glm::vec4 baseColorFactor;
    VkDeviceAddress vertexBuffer;
};

struct LineVertex {
    glm::vec3 position;
    float pad0 = 0.0f;
    glm::vec4 color;
};

struct LineDrawPushConstants {
    glm::mat4 viewProjection;
    VkDeviceAddress vertexBuffer;
};

struct ShadowDrawPushConstants {
    glm::mat4 lightViewProjection;
    glm::mat4 model;
    VkDeviceAddress vertexBuffer;
};

struct SelectionMaskPushConstants {
    glm::mat4 viewProjection;
    glm::mat4 model;
    VkDeviceAddress vertexBuffer;
};

struct SelectionOutlinePushConstants {
    glm::vec4 color;
    glm::vec2 texelSize;
    float thickness;
    float pad0 = 0.0f;
};

struct EffectMeshPushConstants {
    glm::mat4 viewProjection;
    glm::mat4 model;
    glm::vec4 color;
    glm::vec4 params;
    glm::vec4 corruptionColor;
    glm::vec4 corruptionParams;
    glm::vec4 cameraPosition;
    VkDeviceAddress vertexBuffer;
};

//< vbuf_types

//> node_types
struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
struct Node : public IRenderable {

    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        // draw children
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};
//< node_types
//> intro
#define VK_CHECK(x)                                         \
    do {                                                    \
        VkResult err = x;                                   \
        if(err) {                                          \
            std::cout << "Detected Vulkan error: "         \
                      << string_VkResult(err) << "\n";     \
        }                                                   \
    } while(0)
//< intro
