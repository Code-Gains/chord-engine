#include "Core.h"
#include "stb_image.h"
#include <imgui_impl_vulkan.h>
#include <limits>

namespace Engine {

std::optional<std::vector<std::shared_ptr<MeshAsset>>> Core::LoadGltfMeshes(Core* engine, std::filesystem::path filePath)
{
    auto resolvedPath = ResolveProjectPath(filePath);
    auto sourcePath = MakeProjectRelative(resolvedPath);

    auto loadResult = LoadGltfAsset(resolvedPath);
    if (!loadResult.has_value()) {
        return std::nullopt;
    }
    auto& gltf = loadResult.value();

    size_t imageOffset = LoadGltfImages(gltf);
    size_t materialOffset = LoadGltfMaterials(gltf, imageOffset);

    return LoadGltfMeshAssets(engine, gltf, materialOffset, sourcePath);
}

std::optional<std::vector<std::shared_ptr<MeshAsset>>> Core::LoadEngineGltfMeshes(Core* engine, std::filesystem::path filePath)
{
    auto resolvedPath = ResolveEnginePath(filePath);
    auto sourcePath = MakeEngineRelative(resolvedPath);

    auto loadResult = LoadGltfAsset(resolvedPath);
    if (!loadResult.has_value()) {
        return std::nullopt;
    }
    auto& gltf = loadResult.value();

    size_t imageOffset = LoadGltfImages(gltf);
    size_t materialOffset = LoadGltfMaterials(gltf, imageOffset);

    return LoadGltfMeshAssets(engine, gltf, materialOffset, sourcePath);
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

size_t Core::LoadGltfImages(fastgltf::Asset &gltf)
{
    size_t imageOffset = _loadedImages.size();
    _loadedImages.resize(imageOffset + gltf.images.size());

    for (size_t i = 0; i < gltf.images.size(); i++) {
        auto loadedImage = LoadGltfImage(gltf, gltf.images[i]);

        if (loadedImage.has_value()) {
            _loadedImages[imageOffset + i] =
                std::make_shared<AllocatedImage>(loadedImage.value());

            AllocatedImage* imageToDelete = _loadedImages[imageOffset + i].get();

            _mainDeletionQueue.push_function([this, imageToDelete]() {
                DestroyImage(*imageToDelete);
            });
        }
    }

    return imageOffset;
}

size_t Core::LoadGltfMaterials(fastgltf::Asset &gltf, size_t imageOffset)
{
    size_t materialOffset = _loadedMaterials.size();
    _loadedMaterials.resize(materialOffset + gltf.materials.size());

    for (size_t i = 0; i < gltf.materials.size(); i++) {
        auto& gltfMaterial = gltf.materials[i];
        auto& material = _loadedMaterials[materialOffset + i];

        material.image = &_greyImage;
        material.normalImage = &_flatNormalImage;
        material.metallicRoughnessImage = &_defaultMetallicRoughnessImage;
        material.occlusionImage = &_whiteImage;
        material.emissionImage = &_blackImage;
        material.baseColorFactor = glm::vec4{
            gltfMaterial.pbrData.baseColorFactor[0],
            gltfMaterial.pbrData.baseColorFactor[1],
            gltfMaterial.pbrData.baseColorFactor[2],
            gltfMaterial.pbrData.baseColorFactor[3]
        };

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.pbrData.baseColorTexture,
            imageOffset,
            material.image);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.normalTexture,
            imageOffset,
            material.normalImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.pbrData.metallicRoughnessTexture,
            imageOffset,
            material.metallicRoughnessImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.occlusionTexture,
            imageOffset,
            material.occlusionImage);

        AssignGltfMaterialTexture(
            gltf,
            gltfMaterial.emissiveTexture,
            imageOffset,
            material.emissionImage);
    }

    return materialOffset;
}

std::vector<std::shared_ptr<MeshAsset>> Core::LoadGltfMeshAssets(Core *engine, fastgltf::Asset &gltf, size_t materialOffset, const std::filesystem::path& sourcePath)
{
    std::vector<std::shared_ptr<MeshAsset>> meshes;

    for (size_t meshIndex = 0; meshIndex < gltf.meshes.size(); meshIndex++) {
        fastgltf::Mesh& mesh = gltf.meshes[meshIndex];

        MeshAsset newMesh;
        newMesh.name = mesh.name;
        newMesh.source = MeshAssetReference {
            sourcePath.generic_string(),
            static_cast<uint32_t>(meshIndex)
        };

        std::vector<uint32_t> indices;
        std::vector<Vertex> vertices;

        for (auto& primitive : mesh.primitives) {
            LoadGltfPrimitive(gltf, primitive, newMesh, vertices, indices, materialOffset);
        }

        if (!indices.empty() && !vertices.empty()) {
            glm::vec3 minPosition(std::numeric_limits<float>::max());
            glm::vec3 maxPosition(std::numeric_limits<float>::lowest());

            for (const auto& vertex : vertices) {
                minPosition = glm::min(minPosition, vertex.position);
                maxPosition = glm::max(maxPosition, vertex.position);
            }

            newMesh.boundsCenter = (minPosition + maxPosition) * 0.5f;
            newMesh.boundsRadius = glm::max(
                glm::length(maxPosition - newMesh.boundsCenter),
                0.01f);

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

void Core::LoadGltfPrimitive(fastgltf::Asset &gltf, fastgltf::Primitive &primitive, MeshAsset &mesh, std::vector<Vertex> &vertices, std::vector<uint32_t> &indices, size_t materialOffset)
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

    AssignGltfPrimitiveMaterial(primitive, surface, materialOffset);

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

void Core::AssignGltfPrimitiveMaterial(fastgltf::Primitive &primitive, GeoSurface &surface, size_t materialOffset)
{
    if (!primitive.materialIndex.has_value()) {
        return;
    }

    uint32_t materialIndex = primitive.materialIndex.value();

    size_t loadedMaterialIndex = materialOffset + materialIndex;

    if (loadedMaterialIndex < _loadedMaterials.size()) {
        surface.material = &_loadedMaterials[loadedMaterialIndex];
    }
}

} // end of namespace engine
