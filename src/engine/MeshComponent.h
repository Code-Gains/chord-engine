#pragma once
#include <cstdint>
#include <string>
#include <vk_types.h>

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
};

// for rendering single entity add this component
// Default rendering for entities is batched. To avoid increase in query time
// differentiation happens when rendering singles specifically
struct SingleRenderTag {};
