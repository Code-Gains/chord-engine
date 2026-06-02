#pragma once

// ============================================================================
// Standard library
// ============================================================================
#include <array>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

// ============================================================================
// Third-party libraries
// ============================================================================
#include <VkBootstrap.h>
#include <entt/entt.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

// ============================================================================
// Project headers
// ============================================================================
#include "Log.h"
#include "WindowGLFW.h"
#include "vk_descriptors.h"
#include "System.h"
#include "InputSystem.h"
#include "MeshComponent.h"
#include "EcsDebugger.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "vk_mem_alloc.h"
#include "vk_types.h"
#pragma clang diagnostic pop

namespace Engine {

// ============================================================================
// Render data / GPU structs
// ============================================================================
struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    VkBuffer indexBuffer;

    MaterialInstance* material;

    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct MaterialPipeline {
    VkPipeline pipeline;
    VkPipelineLayout layout;
};

struct BatchDrawPushConstants {
    glm::mat4 viewProjection;
    VkDeviceAddress vertexBuffer;
    VkDeviceAddress instanceBuffer;
    uint32_t _pad[2];
};

struct PrefilterPushConstants {
    glm::mat4 viewProjection;
    float roughness;
};

struct InstanceData {
    glm::vec3 position;
    float pad0;         // std430 alignment
    glm::quat rotation; // vec4
    glm::vec3 scale;
    float pad1;
};

struct GPUSceneData {
    glm::vec4 cameraPosition = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    glm::vec4 sunlightDirection = glm::vec4(-0.3f, 1.0f, -0.6f, 3.0f);
    glm::vec4 ambientColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.05f);
};

struct ComputePushConstants {
    glm::vec4 data1;
    glm::vec4 data2;
    glm::vec4 data3;
    glm::vec4 data4;
};

struct ComputeEffect {
    const char* name;

    VkPipeline pipeline;
    VkPipelineLayout layout;

    ComputePushConstants data;
};

// ============================================================================
// Vulkan pipeline create info override / compatibility struct
// ============================================================================
typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkPipelineCreateFlags flags;
    uint32_t stageCount;
    const VkPipelineShaderStageCreateInfo* pStages;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
    const VkPipelineTessellationStateCreateInfo* pTessellationState;
    const VkPipelineViewportStateCreateInfo* pViewportState;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
    const VkPipelineDynamicStateCreateInfo* pDynamicState;
    VkPipelineLayout layout;
    VkRenderPass renderPass;
    uint32_t subpass;
    VkPipeline basePipelineHandle;
    int32_t basePipelineIndex;
} VkGraphicsPipelineCreateInfo;

// ============================================================================
// Descriptor helpers
// ============================================================================
struct DescriptorLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;

    void AddBinding(uint32_t binding, VkDescriptorType type);
    void Clear();
    VkDescriptorSetLayout Build(
        VkDevice device,
        VkShaderStageFlags shaderStages,
        void* pNext = nullptr,
        VkDescriptorSetLayoutCreateFlags flags = 0
    );
};

struct DescriptorAllocator {
    struct PoolSizeRatio {
        VkDescriptorType type;
        float ratio;
    };

    VkDescriptorPool pool;

    void InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
    void ClearDescriptor(VkDevice device);
    void DestroyPool(VkDevice device);

    VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);
};

// ============================================================================
// Lifetime / frame helpers
// ============================================================================
struct DeletionQueue {
    std::deque<std::function<void()>> deletors;

    void push_function(std::function<void()>&& function) {
        deletors.push_back(function);
    }

    void flush() {
        // Reverse iterate the deletion queue to execute all functions in cleanup order.
        for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
            (*it)();
        }

        deletors.clear();
    }
};

struct FrameData {
    VkCommandPool _commandPool;
    VkQueryPool _gpuQueryPool;
    VkCommandBuffer _mainCommandBuffer;
    VkFence _renderFence;
    DeletionQueue _deletionQueue;
    DescriptorAllocatorGrowable _frameDescriptors;
};

constexpr unsigned int FRAME_OVERLAP = 2; // Swapchain image count? TODO

// ============================================================================
// Asset helpers
// ============================================================================
struct CubemapAsset {
    AllocatedImage image;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    uint32_t mipLevels = 1;
};

// ============================================================================
// Threading
// ============================================================================
class Core;

class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    void Enqueue(std::function<void()> job);
    void Wait(); // Wait for all jobs.

    size_t threadCount;

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> jobs;

    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable doneCv;

    bool stop = false;
    size_t activeJobs = 0;
};

// ============================================================================
// Core engine class
// ============================================================================
class Core {
private:
    // ------------------------------------------------------------------------
    // Window / app state
    // ------------------------------------------------------------------------
    std::unique_ptr<WindowGLFW> _window;
    static ThreadPool _threadPool;

    bool _appMinimized = false;
    bool _isInitialized = false;

    // ------------------------------------------------------------------------
    // Initialization steps
    // ------------------------------------------------------------------------
    void InitWindow();
    void InitVulkan();
    void InitQueries();
    void InitSwapchain();
    void InitCommands();
    void InitSyncStructures();
    void InitPipelines();
    void InitBackgroundPipelines();
    void InitDescriptors();
    void InitImgui();
    void InitInstancedMeshPipeline();
    void InitMeshPipeline();
    void InitSkyboxPipeline();
    void InitDefaultData();
    void InitPrefilterPipeline();
    void InitIrradiancePipeline();
    void InitBRDFLUTPipeline();

    // ------------------------------------------------------------------------
    // Vulkan instance / device / surface
    // ------------------------------------------------------------------------
    VkInstance _instance;                         // Vulkan library handle.
    VkDebugUtilsMessengerEXT _debugMessenger;     // Vulkan debug output handle.
    VkPhysicalDevice _chosenGPU;                  // GPU chosen as the default device.
    VkSurfaceKHR _surface;                        // Vulkan window surface.
    VmaAllocator _allocator;

    // ------------------------------------------------------------------------
    // Swapchain
    // ------------------------------------------------------------------------
    VkSwapchainKHR _swapchain;
    VkFormat _swapchainImageFormat;
    VkExtent2D _swapchainExtent;

    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;

    void CreateSwapchain(uint32_t width, uint32_t height);
    void RecreateSwapchain(uint32_t width, uint32_t height);
    void CleanupSwapchainResources();
    void DestroySwapchain();

    // ------------------------------------------------------------------------
    // Frame resources / synchronization
    // ------------------------------------------------------------------------
    uint32_t _frameNumber = 0;
    FrameData _frames[FRAME_OVERLAP];

    std::vector<VkSemaphore> _imageAvailableSemaphores; // size = FRAME_OVERLAP
    std::vector<VkSemaphore> _renderFinishedSemaphores; // size = swapchain image count

    VkQueue _graphicsQueue;
    uint32_t _graphicsQueueFamily;
    DeletionQueue _mainDeletionQueue;

    FrameData& GetCurrentFrame() {
        return _frames[_frameNumber % FRAME_OVERLAP];
    }

    // ------------------------------------------------------------------------
    // Draw targets / render images
    // ------------------------------------------------------------------------
    VkExtent2D _drawExtent;

    VkDescriptorSet _drawImageDescriptors;
    VkDescriptorSetLayout _drawImageDescriptorLayout;
    VkPipelineLayout _gradientPipelineLayout;

    void CreateDrawImages(uint32_t width, uint32_t height);
    void CleanupDrawImages();
    void UpdateDrawImageDescriptor();
    void CleanupDrawImageDescriptors();

    // ------------------------------------------------------------------------
    // Buffers / images / mesh upload helpers
    // ------------------------------------------------------------------------
    AllocatedBuffer CreateBuffer(
        size_t allocSize,
        VkBufferUsageFlags usage,
        VmaMemoryUsage memoryUsage
    );

    void DestroyBuffer(const AllocatedBuffer& buffer);

    GPUMeshBuffers UploadMesh(
        std::span<uint32_t> indices,
        std::span<Vertex> vertices
    );

    AllocatedImage CreateImage(
        VkExtent3D size,
        VkFormat format,
        VkImageUsageFlags usage,
        bool mipmapped = false,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
    );

    AllocatedImage CreateImage(
        void* data,
        VkExtent3D size,
        VkFormat format,
        VkImageUsageFlags usage,
        bool mipmapped = false,
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT
    );

    void DestroyImage(const AllocatedImage& img);

    // ------------------------------------------------------------------------
    // Main drawing flow
    // ------------------------------------------------------------------------
    void Draw();
    void DrawUi();

    void DrawBackground(VkCommandBuffer cmd);
    void DrawGeometry(VkCommandBuffer cmd);
    void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

    // ------------------------------------------------------------------------
    // Immediate submit / ImGui command resources
    // ------------------------------------------------------------------------
    VkFence _immFence;
    VkCommandBuffer _immCommandBuffer;
    VkCommandPool _immCommandPool;

    void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

    // ------------------------------------------------------------------------
    // Background / temporary compute effects
    // ------------------------------------------------------------------------
    std::vector<ComputeEffect> _backgroundEffects;
    int _currentBackgroundEffect{0};

    // ------------------------------------------------------------------------
    // Mesh pipelines / geometry
    // ------------------------------------------------------------------------
    VkPipelineLayout _instancedMeshPipelineLayout;
    VkPipelineLayout _meshPipelineLayout;
    VkPipeline _instancedMeshPipeline;
    VkPipeline _meshPipeline;

    GPUMeshBuffers rectangle;

    // ------------------------------------------------------------------------
    // Default textures / loaded images / samplers
    // ------------------------------------------------------------------------
    AllocatedImage _whiteImage;
    AllocatedImage _blackImage;
    AllocatedImage _greyImage;
    AllocatedImage _flatNormalImage;
    AllocatedImage _defaultMetallicRoughnessImage;
    AllocatedImage _errorCheckerboardImage;
    std::vector<std::shared_ptr<AllocatedImage>> _loadedImages;

    VkSampler _defaultSamplerLinear;
    VkSampler _defaultSamplerNearest;

    // ------------------------------------------------------------------------
    // Descriptor layouts / descriptor sets
    // ------------------------------------------------------------------------
    VkDescriptorSetLayout _singleImageDescriptorLayout;
    VkDescriptorSetLayout _multiImageDescriptorLayout;
    VkDescriptorSetLayout _environmentDescriptorLayout;

    VkDescriptorSet _environmentDescriptorSet;

    // ------------------------------------------------------------------------
    // ECS / systems / update state
    // ------------------------------------------------------------------------
    entt::registry _registry;
    float _deltaTime = 0.0f;

    // ------------------------------------------------------------------------
    // Batched rendering
    // ------------------------------------------------------------------------
    std::unordered_map<MeshAsset*, std::vector<InstanceData>> _batches;
    AllocatedBuffer _instanceBuffer;
    float _timestampPeriod = 0.0f;

    // ------------------------------------------------------------------------
    // Screenshot capture
    // ------------------------------------------------------------------------
    AllocatedBuffer _pendingScreenshot {
        .buffer = VK_NULL_HANDLE,
        .allocation = nullptr,
        .info = {}
    };

    VkExtent2D _pendingScreenshotExtent { 0, 0 };
    VkSubresourceLayout _pendingScreenshotLayout {};

    void CreateScreenshotBuffer();

    // ------------------------------------------------------------------------
    // Materials
    // ------------------------------------------------------------------------
    std::deque<MaterialInstance> _loadedMaterials;
    MaterialInstance _defaultMaterial;

    // ------------------------------------------------------------------------
    // Skybox / environment maps / IBL pipelines
    // ------------------------------------------------------------------------
    AllocatedImage _skyboxImage;

    VkDescriptorSetLayout _skyboxDescriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout _skyboxPipelineLayout = VK_NULL_HANDLE;
    VkPipeline _skyboxPipeline = VK_NULL_HANDLE;

    VkPipeline _prefilterPipeline;
    VkPipelineLayout _prefilterPipelineLayout;

    VkPipeline _irradiancePipeline = VK_NULL_HANDLE;
    VkPipelineLayout _irradiancePipelineLayout = VK_NULL_HANDLE;

    CubemapAsset _skyboxCubemap;
    CubemapAsset _prefilteredCubemap;
    CubemapAsset _irradianceCubemap;

    AllocatedImage CreateCubemap(const std::array<std::string, 6>& facePaths);
    AllocatedImage CreateEmptyCubemap(uint32_t size, VkFormat format, uint32_t mipLevels);

    void GenerateCubemapMipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels);
    void GeneratePrefilteredCubemap();
    void GenerateIrradianceCubemap();

    void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, uint32_t startIndex, uint32_t indexCount);

    // ------------------------------------------------------------------------
    // BRDF LUT
    // ------------------------------------------------------------------------
    AllocatedImage _brdfLUTImage;
    VkSampler _brdfLUTSampler = VK_NULL_HANDLE;

    VkPipeline _brdfLUTPipeline = VK_NULL_HANDLE;
    VkPipelineLayout _brdfLUTPipelineLayout = VK_NULL_HANDLE;

    void GenerateBRDFLUT();

public:
    // ------------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------------
    Core();
    void Init();
    void Run();
    void Shutdown();

    // ------------------------------------------------------------------------
    // Shared thread pool access
    // ------------------------------------------------------------------------
    static ThreadPool& GetThreadPool() {
        return _threadPool;
    }

    // ------------------------------------------------------------------------
    // Public Vulkan / render resources
    // TODO: consider moving some of these behind getters later.
    // ------------------------------------------------------------------------
    DescriptorAllocator globalDescriptorAllocator;
    GPUSceneData sceneData;

    VkDevice _device; // Vulkan device for commands.
    VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;

    AllocatedImage _drawImage;
    AllocatedImage _msaaColorImage;
    AllocatedImage _msaaDepthImage;
    AllocatedImage _depthImage;

    // ------------------------------------------------------------------------
    // Public ECS access
    // ------------------------------------------------------------------------
    entt::registry& GetRegistry();

    // ------------------------------------------------------------------------
    // Public asset/system containers
    // TODO: consider making private
    // ------------------------------------------------------------------------
    std::vector<std::shared_ptr<MeshAsset>> _testMeshes;
    std::vector<std::unique_ptr<System>> _systems;

    // ------------------------------------------------------------------------
    // GLTF loading
    // ------------------------------------------------------------------------
    std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(
        Core* engine,
        std::filesystem::path filePath
    );

    std::optional<AllocatedImage> LoadGltfImage(
        fastgltf::Asset& asset,
        fastgltf::Image& image
    );

    std::optional<fastgltf::Asset> LoadGltfAsset(
        std::filesystem::path filePath
    );

    size_t LoadGltfImages(
        fastgltf::Asset& gltf
    );

    size_t LoadGltfMaterials(
        fastgltf::Asset& gltf,
        size_t imageOffset
    );

    std::vector<std::shared_ptr<MeshAsset>> LoadGltfMeshAssets(
        Core* engine,
        fastgltf::Asset& gltf,
        size_t materialOffset
    );

    void LoadGltfPrimitive(
        fastgltf::Asset& gltf,
        fastgltf::Primitive& primitive,
        MeshAsset& mesh,
        std::vector<Vertex>& vertices,
        std::vector<uint32_t>& indices,
        size_t materialOffset
    );

    void LoadGltfPrimitivePositions(
        fastgltf::Asset& gltf,
        fastgltf::Accessor& positionAccessor,
        std::vector<Vertex>& vertices
    );

    void LoadGltfPrimitiveIndices(
        fastgltf::Asset& gltf,
        fastgltf::Accessor& indexAccessor,
        std::vector<uint32_t>& indices,
        size_t initialVertex
    );

    bool LoadGltfPrimitiveNormals(
        fastgltf::Asset& gltf,
        fastgltf::Primitive& primitive,
        size_t vertexCount,
        size_t initialVertex,
        std::vector<Vertex>& vertices
    );

    bool LoadGltfPrimitiveUVs(
        fastgltf::Asset& gltf,
        fastgltf::Primitive& primitive,
        size_t vertexCount,
        size_t initialVertex,
        std::vector<Vertex>& vertices
    );

    bool LoadGltfPrimitiveTangents(
        fastgltf::Asset& gltf,
        fastgltf::Primitive& primitive,
        size_t vertexCount,
        size_t initialVertex,
        std::vector<Vertex>& vertices
    );

    void AssignGltfPrimitiveMaterial(
        fastgltf::Primitive& primitive,
        GeoSurface& surface,
        size_t materialOffset
    );

    template<typename TextureInfo>
    void AssignGltfMaterialTexture(
        fastgltf::Asset& gltf,
        const std::optional<TextureInfo>& textureInfo,
        size_t imageOffset,
        AllocatedImage*& targetImage
    );
}; // End of Core class

template<typename TextureInfo>
void Core::AssignGltfMaterialTexture(
    fastgltf::Asset& gltf,
    const std::optional<TextureInfo>& textureInfo,
    size_t imageOffset,
    AllocatedImage*& targetImage
)
{
    if (!textureInfo.has_value()) {
        return;
    }

    auto textureIndex = textureInfo.value().textureIndex;
    auto& texture = gltf.textures[textureIndex];

    if (!texture.imageIndex.has_value()) {
        return;
    }

    auto imageIndex = imageOffset + texture.imageIndex.value();

    if (imageIndex < _loadedImages.size() &&
        _loadedImages[imageIndex])
    {
        targetImage = _loadedImages[imageIndex].get();
    }
}

} // namespace Engine
