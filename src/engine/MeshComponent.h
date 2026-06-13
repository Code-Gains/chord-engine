#pragma once
#include <cstdint>
#include <string>
#include <vk_types.h>

#include <glm/vec3.hpp>

struct MeshAssetReference {
    std::string path;
    uint32_t meshIndex = 0;

    bool IsValid() const {
        return !path.empty();
    }
};

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    MaterialInstance* material = nullptr;
};

struct MeshAsset {
    std::string name;
    MeshAssetReference source;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
    glm::vec3 boundsCenter{0.0f};
    float boundsRadius = 1.0f;
};

struct MeshComponent {
    std::shared_ptr<MeshAsset> mesh;
    MeshAssetReference source;
    glm::vec4 baseColorFactor{ 1.0f };
};

struct EffectMeshComponent {
    glm::vec4 color{ 1.0f, 0.45f, 0.08f, 0.45f };
    glm::vec3 velocity{ 0.0f };
    glm::vec3 angularVelocity{ 0.0f };
    float lifetime = 0.35f;
    float age = 0.0f;
    float startScale = 1.0f;
    float endScale = 2.0f;
    float fresnelPower = 2.5f;
    float fresnelIntensity = 2.0f;
    float baseIntensity = 0.2f;
    bool destroyOnComplete = true;
};

// for rendering single entity add this component
// Default rendering for entities is batched. To avoid increase in query time
// differentiation happens when rendering singles specifically
struct SingleRenderTag {};
