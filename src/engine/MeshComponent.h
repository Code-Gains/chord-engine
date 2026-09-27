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
    std::vector<glm::vec3> pickingPositions;
    std::vector<uint32_t> pickingIndices;
    glm::vec3 boundsCenter{0.0f};
    float boundsRadius = 1.0f;
};

struct MeshComponent {
    std::shared_ptr<MeshAsset> mesh;
    MeshAssetReference source;
    std::string materialOverride;
    glm::vec4 baseColorFactor{ 1.0f };
};

struct MeshFlashComponent {
    glm::vec4 color{ 1.0f, 0.12f, 0.04f, 1.0f };
    float amount = 0.0f;
    float timer = 0.0f;
    float duration = 0.12f;
};

struct MeshCorruptionComponent {
    glm::vec4 color{ 0.75f, 0.95f, 1.0f, 1.0f };
    float scale = 8.0f;
    float softness = 0.04f;
    float intensity = 0.35f;
    float amount = 0.0f;
    float age = 0.0f;
    float speed = 1.0f;
};

struct EffectMeshComponent {
    glm::vec4 color{ 1.0f, 0.45f, 0.08f, 0.45f };
    glm::vec4 corruptionColor{ 1.0f, 1.0f, 1.0f, 0.0f };
    glm::vec3 velocity{ 0.0f };
    glm::vec3 angularVelocity{ 0.0f };
    float lifetime = 0.35f;
    float age = 0.0f;
    float startScale = 1.0f;
    float endScale = 2.0f;
    float fresnelPower = 2.5f;
    float fresnelIntensity = 2.0f;
    float baseIntensity = 0.2f;
    float corruptionScale = 8.0f;
    float corruptionSoftness = 0.04f;
    float corruptionIntensity = 0.0f;
    float corruptionAmount = 0.0f;
    bool destroyOnComplete = true;
};

// for rendering single entity add this component
// Default rendering for entities is batched. To avoid increase in query time
// differentiation happens when rendering singles specifically
struct SingleRenderTag {};
