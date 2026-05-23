#pragma once
#include <VkBootstrap.h>
#include <deque>
#include <vector>
#include <functional>
#include "Log.h"
#include "WindowGLFW.h"
#include <filesystem>
#include "vk_descriptors.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <entt/entt.hpp>
#include <mutex>
#include <future>
#include <thread>
#include "System.h"
#include "InputSystem.h"
#include "MeshComponent.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#include "vk_mem_alloc.h"
#include "vk_types.h"
#pragma clang diagnostic pop
// #define VMA_IMPLEMENTATION
// #include "vk_mem_alloc.h"

// ECS PORT
#include "EcsDebugger.h"
#include <queue>

namespace Engine {

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

    // struct MaterialInstance {
    //     MaterialPipeline* pipeline;
    //     VkDescriptorSet materialSet;
    //     MaterialPass passType;
    //     AllocatedImage* image = nullptr;
    //     AllocatedImage* metallicRoughnessImage = nullptr;
    //     AllocatedImage* normalImage = nullptr;
    //     AllocatedImage* occlusionImage = nullptr;
    //     AllocatedImage* emissionImage = nullptr;
    // };

    struct BatchDrawPushConstants {
        glm::mat4 viewProjection;
        VkDeviceAddress vertexBuffer;
        VkDeviceAddress instanceBuffer;
        uint32_t _pad[2];
    };

    // struct InstanceData {
    //     // glm::vec3 position;
    //     // glm::vec4 rotation;
    //     // glm::vec3 scale;
    //     glm::mat4 model;
    // };
    struct InstanceData {
        glm::vec3 position;
        float pad0;        // std430 alignment
        glm::quat rotation; // vec4
        glm::vec3 scale;
        float pad1;
    };

    

    // base class for a renderable dynamic object
    class IRenderable {

        virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
    };

    struct GPUSceneData {
        glm::vec4 cameraPosition;
        glm::vec4 sunlightDirection;
        glm::vec4 ambientColor;
    };

    // struct GeoSurface {
    //     uint32_t startIndex;
    //     uint32_t count;
    // };

    // struct MeshAsset {
    //     std::string name;
    
    //     std::vector<GeoSurface> surfaces;
    //     GPUMeshBuffers meshBuffers;
    // };
    // struct DrawItem {
    //     MeshAsset* mesh;
    //     InstanceData instance;
    // };
    // struct MeshComponent {
    //     std::shared_ptr<MeshAsset> mesh;
    // };


    typedef struct VkGraphicsPipelineCreateInfo {
        VkStructureType                                  sType;
        const void*                                      pNext;
        VkPipelineCreateFlags                            flags;
        uint32_t                                         stageCount;
        const VkPipelineShaderStageCreateInfo*           pStages;
        const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
        const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
        const VkPipelineTessellationStateCreateInfo*     pTessellationState;
        const VkPipelineViewportStateCreateInfo*         pViewportState;
        const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
        const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
        const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
        const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
        const VkPipelineDynamicStateCreateInfo*          pDynamicState;
        VkPipelineLayout                                 layout;
        VkRenderPass                                     renderPass;
        uint32_t                                         subpass;
        VkPipeline                                       basePipelineHandle;
        int32_t                                          basePipelineIndex;
    } VkGraphicsPipelineCreateInfo;
    

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
    
    struct DescriptorLayoutBuilder {
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        void AddBinding(uint32_t binding, VkDescriptorType type);
        void Clear();
        VkDescriptorSetLayout Build(VkDevice device, VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
    };

    struct DescriptorAllocator {
        struct PoolSizeRatio{
            VkDescriptorType type;
            float ratio;
        };

        VkDescriptorPool pool;

        void InitPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
        void ClearDescriptor(VkDevice device);
        void DestroyPool(VkDevice device);

        VkDescriptorSet Allocate(VkDevice device, VkDescriptorSetLayout layout);
    };


    struct DeletionQueue
    {
        std::deque<std::function<void()>> deletors;

        void push_function(std::function<void()>&& function) {
            deletors.push_back(function);
        }

        void flush() {
            // reverse iterate the deletion queue to execute all the functions
            for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
                (*it)(); //call functors
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

    struct CubemapAsset {
        AllocatedImage image;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        uint32_t mipLevels = 1;
    };


    class Core;
    // struct GLTFMetallic_Roughness {
    //     MaterialPipeline opaquePipeline;
    //     MaterialPipeline transparentPipeline;

    //     VkDescriptorSetLayout materialLayout;

    //     struct MaterialConstants {
    //         glm::vec4 colorFactors;
    //         glm::vec4 metal_rough_factors;
    //         //padding, we need it anyway for uniform buffers
    //         glm::vec4 extra[14];
    //     };

    //     struct MaterialResources {
    //         AllocatedImage colorImage;
    //         VkSampler colorSampler;
    //         AllocatedImage metalRoughImage;
    //         VkSampler metalRoughSampler;
    //         VkBuffer dataBuffer;
    //         uint32_t dataBufferOffset;
    //     };

    //     DescriptorWriter writer;

    //     void BuildPipelines(Core* engine);
    //     void ClearResources(VkDevice device);

    //     MaterialInstance WriteMaterial(VkDevice device, MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
    // };


class ThreadPool {
public:
    ThreadPool(size_t numThreads);
    ~ThreadPool();

    void Enqueue(std::function<void()> job);
    void Wait(); // wait for all jobs

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

    class Core {
        std::unique_ptr<WindowGLFW> _window;
        static ThreadPool _threadPool;
        void InitWindow();
        void InitVulkan();
        void InitQueries();
        void InitSwapchain();
        void InitCommands();
        void InitSyncStructures();
        void InitPipelines();
	    void InitBackgroundPipelines();
        void InitDescriptors();
        bool _appMinimized = false;
        bool _isInitialized = false;

        // Vulkan
        VkInstance _instance;// Vulkan library handle
        VkDebugUtilsMessengerEXT _debugMessenger;// Vulkan debug output handle
        VkPhysicalDevice _chosenGPU;// GPU chosen as the default device
        VkSurfaceKHR _surface; // Vulkan window surface
        VkSwapchainKHR _swapchain;
        VkFormat _swapchainImageFormat;

        std::vector<VkImage> _swapchainImages;
        std::vector<VkImageView> _swapchainImageViews;
        VkExtent2D _swapchainExtent;

        VmaAllocator _allocator;

        void CreateSwapchain(uint32_t width, uint32_t height);
        void RecreateSwapchain(uint32_t width, uint32_t height);
        void CleanupSwapchainResources();
	    void DestroySwapchain();
        void UpdateDrawImageDescriptor();
        void CleanupDrawImageDescriptors();
        void CreateScreenshotBuffer();



        void CreateDrawImages(uint32_t width, uint32_t height);
        void CleanupDrawImages();
        // Graphics
        uint32_t _frameNumber = 0;
        FrameData _frames[FRAME_OVERLAP];
        FrameData& GetCurrentFrame() { return _frames[_frameNumber % FRAME_OVERLAP]; };
        std::vector<VkSemaphore> _imageAvailableSemaphores; // size = FRAME_OVERLAP
        std::vector<VkSemaphore> _renderFinishedSemaphores; // size = to swapchain image count

        VkQueue _graphicsQueue;
        uint32_t _graphicsQueueFamily;
        DeletionQueue _mainDeletionQueue;

        //AllocatedImage _drawImage;
        //AllocatedImage _depthImage;
	    VkExtent2D _drawExtent;


        VkDescriptorSet _drawImageDescriptors;
        VkDescriptorSetLayout _drawImageDescriptorLayout;
        //VkPipeline _gradientPipeline;
	    VkPipelineLayout _gradientPipelineLayout;

        AllocatedBuffer CreateBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        void DestroyBuffer(const AllocatedBuffer& buffer);
        GPUMeshBuffers UploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

        AllocatedImage CreateImage(VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        AllocatedImage CreateImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped = false, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT);
        void DestroyImage(const AllocatedImage& img);


        void Draw();
        void DrawUi();

        // Drawing helpers
        void DrawBackground(VkCommandBuffer cmd);
        void DrawGeometry(VkCommandBuffer cmd);
        void DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView);

        // Imgui
        VkFence _immFence;
        VkCommandBuffer _immCommandBuffer;
        VkCommandPool _immCommandPool;

        
        void ImmediateSubmit(std::function<void(VkCommandBuffer cmd)>&& function);

        void InitImgui();

        // temp
        std::vector<ComputeEffect> _backgroundEffects;
        int _currentBackgroundEffect{0};
        VkPipelineLayout _trianglePipelineLayout;
        VkPipeline _trianglePipeline;

        //void InitTrianglePipeline();

        VkPipelineLayout _instancedMeshPipelineLayout;
        VkPipelineLayout _meshPipelineLayout;
        VkPipeline _instancedMeshPipeline;
        VkPipeline _meshPipeline;

        GPUMeshBuffers rectangle;

        void InitInstancedMeshPipeline();
        void InitMeshPipeline();
        void InitSkyboxPipeline();
        void InitDefaultData();
        void InitBRDFLUTPipeline();


        AllocatedImage _whiteImage;
        AllocatedImage _blackImage;
        AllocatedImage _greyImage;
        AllocatedImage _errorCheckerboardImage;
        std::vector<std::shared_ptr<AllocatedImage>> _loadedImages;

        VkSampler _defaultSamplerLinear;
        VkSampler _defaultSamplerNearest;

        VkDescriptorSetLayout _singleImageDescriptorLayout;
        VkDescriptorSetLayout _multiImageDescriptorLayout;
        VkDescriptorSetLayout _environmentDescriptorLayout;

        VkDescriptorSet _environmentDescriptorSet;

        //EcsDebugger _ecsDebugger;
        entt::registry _registry;

        // Systems
        //InputSystem _inputSystem;

        // update trackers
        float _deltaTime = 0.0f;

        // Batched
        std::unordered_map<MeshAsset*, std::vector<InstanceData>> _batches;   
        //std::vector<DrawItem> _drawItems;
        AllocatedBuffer _instanceBuffer;
        float _timestampPeriod = 0.0f;

        // screenshot
        // VkBuffer _screenshotBuffer;
        // VkDeviceMemory _screenshotMemory;
        AllocatedBuffer _pendingScreenshot { .buffer = VK_NULL_HANDLE, .allocation = nullptr, .info = {} };
        VkExtent2D      _pendingScreenshotExtent { 0, 0 };
        VkSubresourceLayout _pendingScreenshotLayout {};

        std::vector<MaterialInstance> _loadedMaterials;
        MaterialInstance _defaultMaterial;

        AllocatedImage _skyboxImage;
        //VkSampler _skyboxSampler = VK_NULL_HANDLE;
        VkDescriptorSetLayout _skyboxDescriptorLayout = VK_NULL_HANDLE;
        VkPipelineLayout _skyboxPipelineLayout = VK_NULL_HANDLE;
        VkPipeline _skyboxPipeline = VK_NULL_HANDLE;

        CubemapAsset _skyboxCubemap;
        AllocatedImage CreateCubemap(const std::array<std::string, 6>& facePaths);

        void GenerateCubemapMipmaps(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height, uint32_t mipLevels);
        void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, uint32_t startIndex, uint32_t indexCount);

        AllocatedImage _brdfLUTImage;
        VkSampler _brdfLUTSampler = VK_NULL_HANDLE;

        VkPipeline _brdfLUTPipeline = VK_NULL_HANDLE;
        VkPipelineLayout _brdfLUTPipelineLayout = VK_NULL_HANDLE;

        void GenerateBRDFLUT();

    public:
        Core();
        void Init();
        void Run(); // main loop
        void Shutdown();

        static ThreadPool& GetThreadPool() { return _threadPool; }

        DescriptorAllocator globalDescriptorAllocator;
        GPUSceneData sceneData;
        VkDevice _device; // Vulkan device for commands
        VkDescriptorSetLayout _gpuSceneDataDescriptorLayout;
        AllocatedImage _drawImage;
        AllocatedImage _msaaColorImage;
        AllocatedImage _msaaDepthImage;
        AllocatedImage _depthImage;
        //MaterialInstance defaultData;
        //GLTFMetallic_Roughness metalRoughMaterial;
        // Getters
        entt::registry& GetRegistry();

        // TODO think how to structure public private vars
        std::vector<std::shared_ptr<MeshAsset>> _testMeshes;
        std::vector<std::unique_ptr<System>> _systems;

        std::optional<std::vector<std::shared_ptr<MeshAsset>>> LoadGltfMeshes(Core* engine, std::filesystem::path filePath);
        std::optional<AllocatedImage> LoadGltfImage(fastgltf::Asset& asset, fastgltf::Image& image);
    };
} // namespace Engine