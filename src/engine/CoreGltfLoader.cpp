#include "Core.h"
#include "stb_image.h"
#include <imgui_impl_vulkan.h>

namespace Engine {

std::optional<std::vector<std::shared_ptr<MeshAsset>>> Core::LoadGltfMeshes(Core* engine, std::filesystem::path filePath)
{
    auto loadResult = LoadGltfAsset(filePath);
    if (!loadResult.has_value()) {
        return std::nullopt;
    }
    auto& gltf = loadResult.value();

    LoadGltfImages(gltf);
    LoadGltfMaterials(gltf);

    return LoadGltfMeshAssets(engine, gltf);
}

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

std::optional<fastgltf::Asset> Core::LoadGltfAsset(std::filesystem::path filePath)
{
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (!dataResult) {
        return std::nullopt;
    }

    fastgltf::GltfDataBuffer data = std::move(dataResult.get());

    constexpr auto gltfOptions = fastgltf::Options::LoadExternalBuffers;

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

        return std::move(load.get());
    }

    if (extension == ".gltf") {
        auto load = parser.loadGltf(data, parentPath, gltfOptions);
        if (!load) {
            return std::nullopt;
        }

        return std::move(load.get());
    }

    return std::nullopt;
}

void Core::LoadGltfImages(fastgltf::Asset &gltf)
{
    _loadedImages.clear();
    _loadedImages.resize(gltf.images.size());

    for (size_t i = 0; i < gltf.images.size(); i++) {
        auto loadedImage = LoadGltfImage(gltf, gltf.images[i]);

        if (loadedImage.has_value()) {
            _loadedImages[i] =
                std::make_shared<AllocatedImage>(loadedImage.value());

            AllocatedImage* imageToDelete = _loadedImages[i].get();

            _mainDeletionQueue.push_function([this, imageToDelete]() {
                DestroyImage(*imageToDelete);
            });
        }
    }
}

void Core::LoadGltfMaterials(fastgltf::Asset &gltf)
{
    _loadedMaterials.clear();
    _loadedMaterials.resize(gltf.materials.size());

    for (size_t i = 0; i < gltf.materials.size(); i++) {
        auto& gltfMaterial = gltf.materials[i];
        auto& material = _loadedMaterials[i];

        material.image = &_errorCheckerboardImage;
        material.normalImage = &_errorCheckerboardImage;
        material.metallicRoughnessImage = &_whiteImage;
        material.occlusionImage = &_whiteImage;
        material.emissionImage = &_blackImage;

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.pbrData.baseColorTexture,
            material.image);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.normalTexture,
            material.normalImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.pbrData.metallicRoughnessTexture,
            material.metallicRoughnessImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.occlusionTexture,
            material.occlusionImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.emissiveTexture,
            material.emissionImage);
    }
}

std::vector<std::shared_ptr<MeshAsset>> Core::LoadGltfMeshAssets(Core *engine, fastgltf::Asset &gltf)
{
    std::vector<std::shared_ptr<MeshAsset>> meshes;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        MeshAsset newMesh;
        newMesh.name = mesh.name;

        std::vector<uint32_t> indices;
        std::vector<Vertex> vertices;

        for (auto& primitive : mesh.primitives) {
            LoadGltfPrimitive(gltf, primitive, newMesh, vertices, indices);
        }

        if (!indices.empty() && !vertices.empty()) {
            newMesh.meshBuffers = engine->UploadMesh(indices, vertices);

            auto vertexBuffer = newMesh.meshBuffers.vertexBuffer;
            auto indexBuffer = newMesh.meshBuffers.indexBuffer;

            _mainDeletionQueue.push_function([this, vertexBuffer, indexBuffer]() {
                DestroyBuffer(vertexBuffer);
                DestroyBuffer(indexBuffer);
            });

            meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newMesh)));
        }
    }

    return meshes;
}

void Core::LoadGltfPrimitive(fastgltf::Asset &gltf, fastgltf::Primitive &primitive, MeshAsset &mesh, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices)
{
    if (!primitive.indicesAccessor.has_value()) {
        return;
    }

    GeoSurface surface{};
    surface.startIndex = static_cast<uint32_t>(indices.size());
    surface.material = nullptr;

    auto& indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];

    if (indexAccessor.type != fastgltf::AccessorType::Scalar) {
        return;
    }

    surface.count = static_cast<uint32_t>(indexAccessor.count);

    fastgltf::Attribute* positionAttr = primitive.findAttribute("POSITION");
    if (!positionAttr) {
        return;
    }

    auto& positionAccessor = gltf.accessors[positionAttr->accessorIndex];

    if (positionAccessor.type != fastgltf::AccessorType::Vec3) {
        return;
    }

    const size_t initialVertex = vertices.size();

    LoadGltfPrimitivePositions(gltf, positionAccessor, vertices);
    LoadGltfPrimitiveIndices(gltf, indexAccessor, indices, initialVertex);

    bool hasNormals = LoadGltfPrimitiveNormals(
        gltf, primitive, positionAccessor.count, initialVertex, vertices);

    bool hasUVs = LoadGltfPrimitiveUVs(
        gltf, primitive, positionAccessor.count, initialVertex, vertices);

    bool hasTangents = LoadGltfPrimitiveTangents(
        gltf, primitive, positionAccessor.count, initialVertex, vertices);

    AssignGltfPrimitiveMaterial(primitive, surface);

    if (!hasTangents && hasNormals && hasUVs) {
        GenerateTangents(
            vertices,
            indices,
            surface.startIndex,
            surface.count);
    }

    mesh.surfaces.push_back(surface);
}

void Core::LoadGltfPrimitivePositions(fastgltf::Asset &gltf, fastgltf::Accessor &positionAccessor, std::vector<Vertex> &vertices)
{
     const size_t initialVertex = vertices.size();

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

            vertices[initialVertex + index] = v;
        });
}

void Core::LoadGltfPrimitiveIndices(fastgltf::Asset &gltf, fastgltf::Accessor &indexAccessor, std::vector<uint32_t> &indices, size_t initialVertex)
{
    indices.reserve(indices.size() + indexAccessor.count);

    fastgltf::iterateAccessor<std::uint32_t>(
        gltf,
        indexAccessor,
        [&](std::uint32_t index) {
            indices.push_back(index + static_cast<uint32_t>(initialVertex));
        });
}

bool Core::LoadGltfPrimitiveNormals(fastgltf::Asset &gltf, fastgltf::Primitive &primitive, size_t vertexCount, size_t initialVertex, std::vector<Vertex> &vertices)
{
    auto* normalsAttr = primitive.findAttribute("NORMAL");
    if (!normalsAttr) {
        return false;
    }

    auto& normalAccessor = gltf.accessors[normalsAttr->accessorIndex];

    if (normalAccessor.type != fastgltf::AccessorType::Vec3 ||
        normalAccessor.count != vertexCount) {
        return false;
    }

    fastgltf::iterateAccessorWithIndex<glm::vec3>(
        gltf,
        normalAccessor,
        [&](glm::vec3 normal, size_t index) {
            vertices[initialVertex + index].normal = normal;
        });

    return true;
}

bool Core::LoadGltfPrimitiveUVs(fastgltf::Asset &gltf, fastgltf::Primitive &primitive, size_t vertexCount, size_t initialVertex, std::vector<Vertex> &vertices)
{
    auto* uvsAttr = primitive.findAttribute("TEXCOORD_0");
    if (!uvsAttr) {
        return false;
    }

    auto& uvAccessor = gltf.accessors[uvsAttr->accessorIndex];

    if (uvAccessor.type != fastgltf::AccessorType::Vec2 ||
        uvAccessor.count != vertexCount) {
        return false;
    }

    fastgltf::iterateAccessorWithIndex<glm::vec2>(
        gltf,
        uvAccessor,
        [&](glm::vec2 uv, size_t index) {
            vertices[initialVertex + index].uv_x = uv.x;
            vertices[initialVertex + index].uv_y = uv.y;
        });

    return true;
}

bool Core::LoadGltfPrimitiveTangents(fastgltf::Asset &gltf, fastgltf::Primitive &primitive, size_t vertexCount, size_t initialVertex, std::vector<Vertex> &vertices)
{
    auto* tangentAttr = primitive.findAttribute("TANGENT");
    if (!tangentAttr) {
        return false;
    }

    auto& tangentAccessor = gltf.accessors[tangentAttr->accessorIndex];

    if (tangentAccessor.type != fastgltf::AccessorType::Vec4 ||
        tangentAccessor.count != vertexCount) {
        return false;
    }

    fastgltf::iterateAccessorWithIndex<glm::vec4>(
        gltf,
        tangentAccessor,
        [&](glm::vec4 tangent, size_t index) {
            vertices[initialVertex + index].tangent = tangent;
        });

    return true;
}

void Core::AssignGltfPrimitiveMaterial(fastgltf::Primitive &primitive, GeoSurface &surface)
{
    if (!primitive.materialIndex.has_value()) {
        return;
    }

    uint32_t materialIndex = primitive.materialIndex.value();

    if (materialIndex < _loadedMaterials.size()) {
        surface.material = &_loadedMaterials[materialIndex];
    }
}

} // end of namespace engine