#pragma once
#include <vk_types.h>

struct GeoSurface {
    uint32_t startIndex;
    uint32_t count;
    MaterialInstance* material = nullptr;
};

struct MeshAsset {
    std::string name;

    std::vector<GeoSurface> surfaces;
    GPUMeshBuffers meshBuffers;
};

struct MeshComponent {
    std::shared_ptr<MeshAsset> mesh;
};

// for rendering single entity add this component
// Default rendering for entities is batched. To avoid increase in query time
// differentiation happens when rendering singles specifically
struct SingleRenderTag {};