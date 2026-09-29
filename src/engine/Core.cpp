#include <chrono>
using Clock = std::chrono::high_resolution_clock;
#include <cmath>
#include "Core.h"
//#include "VkInit.h"
#include "vk_initializers.h"
#include "vk_images.h"
#include "vk_pipelines.h"
#include "Transform.h"
#include "Camera.h"
#include "CameraSystem.h"
#include "CinematicCameraSystem.h"
#include "LineComponent.h"
#include "NameComponent.h"
#include "WorldSerializer.h"
#include "EditorSelection.h"
#include "EditorCaptureState.h"
#include "EnvironmentComponent.h"
#include "EntityState.h"
#include "HierarchySystem.h"
#include "JoltPhysicsSystem.h"
#include "HeightFogComponent.h"
#include "ScreenPostProcessComponent.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <numeric>
#include <sstream>
#include <typeinfo>
#include <unordered_map>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_image.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#define VMA_IMPLEMENTATION
#include "vk_mem_alloc.h"
#include "vk_descriptors.h"
#pragma clang diagnostic pop

// ECS PORT
#include "EcsDebugger.h"
#include "SunlightComponent.h"
#include "ImGuiWindowRegistry.h"

#include <glm/gtc/matrix_transform.hpp>

namespace Engine {
namespace {

struct BenchmarkTiming {
    double totalMs = 0.0;
    uint64_t calls = 0;
};

float Float16ToFloat32(uint16_t h16)
{
    const uint32_t sign = (h16 >> 15) & 0x1;
    const uint32_t exponent = (h16 >> 10) & 0x1F;
    const uint32_t mantissa = h16 & 0x3FF;

    float value;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    }
    else if (exponent == 31) {
        value = 0.0f;
    }
    else {
        value = std::ldexp(static_cast<float>(mantissa | 0x400), static_cast<int>(exponent) - 25);
    }

    return sign ? -value : value;
}

uint8_t LinearFloatToPngByte(float value)
{
    if (!std::isfinite(value)) {
        value = 0.0f;
    }

    value = std::clamp(value, 0.0f, 1.0f);
    value = std::pow(value, 1.0f / 2.2f);
    return static_cast<uint8_t>(value * 255.0f + 0.5f);
}

glm::vec4 ResolveMeshFlash(const entt::registry& registry, entt::entity entity)
{
    const auto* flash = registry.try_get<MeshFlashComponent>(entity);
    if (!flash || flash->amount <= 0.0f || flash->timer <= 0.0f) {
        return glm::vec4{ 1.0f, 0.12f, 0.04f, 0.0f };
    }

    const float duration = std::max(0.001f, flash->duration);
    const float fade = std::clamp(flash->timer / duration, 0.0f, 1.0f);
    return glm::vec4{
        flash->color.r,
        flash->color.g,
        flash->color.b,
        std::clamp(flash->amount * fade, 0.0f, 1.0f)
    };
}

glm::vec4 ResolveMeshCorruptionColor(const entt::registry& registry, entt::entity entity)
{
    const auto* corruption = registry.try_get<MeshCorruptionComponent>(entity);
    if (!corruption || corruption->amount <= 0.0f) {
        return glm::vec4{ 0.75f, 0.95f, 1.0f, 0.0f };
    }

    return glm::vec4{
        corruption->color.r,
        corruption->color.g,
        corruption->color.b,
        std::clamp(corruption->amount * corruption->color.a, 0.0f, 1.0f)
    };
}

glm::vec4 ResolveMeshCorruptionParams(const entt::registry& registry, entt::entity entity)
{
    const auto* corruption = registry.try_get<MeshCorruptionComponent>(entity);
    if (!corruption) {
        return glm::vec4{ 8.0f, 0.04f, 0.35f, 0.0f };
    }

    return glm::vec4{
        std::max(0.001f, corruption->scale),
        std::clamp(corruption->softness, 0.001f, 1.0f),
        std::max(0.0f, corruption->intensity),
        corruption->age
    };
}

float MaxAbsScaleAxis(const glm::vec3& scale)
{
    const glm::vec3 absScale = glm::abs(scale);
    return glm::max(absScale.x, glm::max(absScale.y, absScale.z));
}

EditorCaptureState& GetEditorCaptureState(entt::registry& registry)
{
    if (!registry.ctx().contains<EditorCaptureState>()) {
        return registry.ctx().emplace<EditorCaptureState>();
    }

    return registry.ctx().get<EditorCaptureState>();
}

entt::entity ResolveCinematicCameraEntity(entt::registry& registry)
{
    if (registry.ctx().contains<EditorSelection>()) {
        const auto& selection = registry.ctx().get<EditorSelection>();
        const entt::entity selectedEntity = selection.selectedEntity;
        if (selectedEntity != entt::null &&
            registry.valid(selectedEntity) &&
            registry.all_of<Camera, Transform, CinematicCameraShotComponent>(selectedEntity) &&
            !IsEntityDisabled(registry, selectedEntity)) {
            return selectedEntity;
        }
    }

    auto activeView = registry.view<Camera, Transform, CinematicCameraShotComponent, ActiveCameraTag>(
        entt::exclude<DisabledEntityTag>);
    for (auto entity : activeView) {
        if (!IsEntityDisabled(registry, entity)) {
            return entity;
        }
    }

    auto shotView = registry.view<Camera, Transform, CinematicCameraShotComponent>(
        entt::exclude<DisabledEntityTag>);
    for (auto entity : shotView) {
        if (!IsEntityDisabled(registry, entity)) {
            return entity;
        }
    }

    return entt::null;
}

void SetActiveCamera(entt::registry& registry, entt::entity cameraEntity)
{
    std::vector<entt::entity> previousActiveCameras;
    auto activeView = registry.view<ActiveCameraTag>();
    for (auto entity : activeView) {
        if (entity != cameraEntity) {
            previousActiveCameras.push_back(entity);
        }
    }

    for (auto entity : previousActiveCameras) {
        if (registry.valid(entity)) {
            registry.remove<ActiveCameraTag>(entity);
        }
    }

    if (cameraEntity != entt::null &&
        registry.valid(cameraEntity) &&
        !registry.all_of<ActiveCameraTag>(cameraEntity)) {
        registry.emplace<ActiveCameraTag>(cameraEntity);
    }
}

void SetCaptureUiHidden(entt::registry& registry, bool hidden)
{
    auto& capture = GetEditorCaptureState(registry);
    if (capture.uiHidden == hidden) {
        return;
    }

    capture.uiHidden = hidden;
    if (auto* windowRegistry = registry.ctx().find<ImGuiWindowRegistry>()) {
        if (hidden) {
            windowRegistry->HideAllWindows();
        }
        else {
            windowRegistry->RestoreHiddenWindows();
        }
    }
}

void StartCinematicCapture(entt::registry& registry)
{
    const entt::entity cameraEntity = ResolveCinematicCameraEntity(registry);
    if (cameraEntity == entt::null) {
        return;
    }

    auto shotView = registry.view<CinematicCameraShotComponent>();
    for (auto entity : shotView) {
        shotView.get<CinematicCameraShotComponent>(entity).playing = false;
    }

    auto& shot = registry.get<CinematicCameraShotComponent>(cameraEntity);
    shot.time = 0.0f;
    shot.playing = true;
    SetActiveCamera(registry, cameraEntity);
    SetCaptureUiHidden(registry, true);
}

void HandleCaptureShortcuts(entt::registry& registry)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl || !io.KeyShift) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        StartCinematicCapture(registry);
    }

    if (ImGui::IsKeyPressed(ImGuiKey_U, false)) {
        auto& capture = GetEditorCaptureState(registry);
        SetCaptureUiHidden(registry, !capture.uiHidden);
    }
}

} // namespace

#ifndef NDEBUG
    constexpr bool gUseValidationLayers = true; // TODO move to global settings later
#else
    constexpr bool gUseValidationLayers = false; // TODO move to global settings later
#endif

ThreadPool Core::_threadPool{std::thread::hardware_concurrency()};

Core::Core()
{
}

void Core::SetEngineRoot(std::filesystem::path root)
{
    _engineRoot = std::filesystem::weakly_canonical(std::move(root));
}

const std::filesystem::path& Core::GetEngineRoot() const
{
    return _engineRoot;
}

std::filesystem::path Core::ResolveEnginePath(const std::filesystem::path& path) const
{
    if (path.is_absolute()) {
        return path;
    }

    return _engineRoot / path;
}

std::filesystem::path Core::MakeEngineRelative(const std::filesystem::path& path) const
{
    std::filesystem::path resolvedPath =
        path.is_absolute() ? path : std::filesystem::weakly_canonical(path);

    std::error_code error;
    auto relativePath = std::filesystem::relative(resolvedPath, _engineRoot, error);

    if (error || relativePath.empty()) {
        return path.generic_string();
    }

    return relativePath.generic_string();
}

void Core::SetProjectRoot(std::filesystem::path root)
{
    _projectRoot = std::filesystem::weakly_canonical(std::move(root));
}

const std::filesystem::path& Core::GetProjectRoot() const
{
    return _projectRoot;
}

std::filesystem::path Core::ResolveProjectPath(const std::filesystem::path& path) const
{
    if (path.is_absolute()) {
        return path;
    }

    return _projectRoot / path;
}

std::filesystem::path Core::MakeProjectRelative(const std::filesystem::path& path) const
{
    std::filesystem::path resolvedPath =
        path.is_absolute() ? path : std::filesystem::weakly_canonical(path);

    std::error_code error;
    auto relativePath = std::filesystem::relative(resolvedPath, _projectRoot, error);

    if (error || relativePath.empty()) {
        return path.generic_string();
    }

    return relativePath.generic_string();
}

bool Core::PlayProjectAudioOneShot(
    const std::filesystem::path& path,
    float gain,
    float pitch)
{
    return _audioSystem.PlayWavOneShot(ResolveProjectPath(path), gain, pitch);
}

AudioLoopHandle Core::StartProjectAudioLoop(
    const std::filesystem::path& path,
    float gain,
    float pitch)
{
    return _audioSystem.StartWavLoop(ResolveProjectPath(path), gain, pitch);
}

bool Core::UpdateProjectAudioLoop(
    AudioLoopHandle handle,
    float gain,
    float pitch)
{
    return _audioSystem.SetLoopParameters(handle, gain, pitch);
}

void Core::StopProjectAudioLoop(AudioLoopHandle handle)
{
    _audioSystem.StopLoop(handle);
}

bool Core::PlayProjectSoundCue(
    const std::filesystem::path& path,
    float gainScale,
    float pitchScale)
{
    return _audioSystem.PlaySoundCue(
        ResolveProjectPath(path),
        _projectRoot,
        gainScale,
        pitchScale);
}

void Core::InvalidateProjectSoundCue(const std::filesystem::path& path)
{
    _audioSystem.InvalidateSoundCue(ResolveProjectPath(path));
}

EditorMode Core::GetEditorMode() const
{
    return _editorMode;
}

bool Core::IsPlayMode() const
{
    return _editorMode == EditorMode::Play;
}

bool Core::IsEditorWireframeEnabled() const
{
    return _editorWireframeEnabled;
}

void Core::SetEditorWireframeEnabled(bool enabled)
{
    _editorWireframeEnabled = enabled;
}

void Core::ToggleEditorWireframe()
{
    _editorWireframeEnabled = !_editorWireframeEnabled;
}

void Core::SetRetainCpuMeshPickingData(bool enabled)
{
    _retainCpuMeshPickingData = enabled;
}

void Core::StartPlayMode()
{
    if (_editorMode == EditorMode::Play) {
        return;
    }

    auto serializer = CreateWorldSerializer();
    _playModeSnapshot = serializer.SaveWorldToJson(*this);
    _editorMode = EditorMode::Play;
    for (auto& system : _systems) {
        system->OnPlayStart();
    }
}

void Core::StopPlayMode()
{
    if (_editorMode == EditorMode::Edit) {
        return;
    }

    for (auto& system : _systems) {
        system->OnPlayStop();
    }

    auto serializer = CreateWorldSerializer();
    serializer.LoadWorldFromJson(*this, _playModeSnapshot);
    _playModeSnapshot = nlohmann::json();
    _editorMode = EditorMode::Edit;
}

const std::optional<std::filesystem::path>& Core::GetCurrentWorldPath() const
{
    return _currentWorldPath;
}

void Core::SetCurrentWorldPath(std::filesystem::path path)
{
    if (path.is_absolute()) {
        path = MakeProjectRelative(path);
    }

    _currentWorldPath = std::move(path);
}

void Core::ClearCurrentWorldPath()
{
    _currentWorldPath = std::nullopt;
}

void Core::RegisterComponentSerializers(std::function<void(ComponentSerializerRegistry&)> setup)
{
    _componentSerializerSetups.push_back(std::move(setup));
}

WorldSerializer Core::CreateWorldSerializer() const
{
    WorldSerializer serializer;

    for (const auto& setup : _componentSerializerSetups) {
        setup(serializer.ComponentSerializers());
    }

    return serializer;
}

MaterialAsset& Core::RegisterMaterialAsset(std::string name, MaterialInstance material)
{
    if (auto existing = _materialAssetIndicesByName.find(name); existing != _materialAssetIndicesByName.end()) {
        auto& asset = _materialAssets[existing->second];
        asset.material = material;
        return asset;
    }

    auto& asset = _materialAssets.emplace_back();
    asset.name = std::move(name);
    asset.material = material;
    _materialAssetIndicesByName.emplace(asset.name, _materialAssets.size() - 1);
    return asset;
}

MaterialInstance* Core::FindMaterial(std::string_view name)
{
    auto it = _materialAssetIndicesByName.find(std::string{ name });
    if (it == _materialAssetIndicesByName.end()) {
        return nullptr;
    }

    return &_materialAssets[it->second].material;
}

const MaterialInstance* Core::FindMaterial(std::string_view name) const
{
    auto it = _materialAssetIndicesByName.find(std::string{ name });
    if (it == _materialAssetIndicesByName.end()) {
        return nullptr;
    }

    return &_materialAssets[it->second].material;
}

const std::deque<MaterialAsset>& Core::GetMaterialAssets() const
{
    return _materialAssets;
}

MaterialInstance* Core::ResolveMeshMaterial(const MeshComponent& meshComponent, const GeoSurface& surface)
{
    if (!meshComponent.materialOverride.empty()) {
        if (auto* material = FindMaterial(meshComponent.materialOverride)) {
            return material;
        }
    }

    return surface.material;
}

void Core::Init()
{
#ifdef DEBUG
    ENGINE_LOG_INFO("Engine initializing in DEBUG mode!");
#else
    ENGINE_LOG_INFO("Engine initializing in RELEASE mode!");
#endif
    auto inputEntity = _registry.create();
    _registry.emplace<InputState>(inputEntity);

    // Sets up GLFW Window with Vulkan Prerequisites
    InitWindow();
    InitVulkan();
    InitQueries();
    InitSwapchain();
    InitImgui();
    InitCommands();
    InitSyncStructures();
    InitDescriptors();
    InitShadowResources();
    InitPipelines();
    InitDefaultData();
    if (_audioSystem.Init()) {
        ENGINE_LOG_INFO("Audio system initialized.");
    }
    else {
        ENGINE_LOG_ERROR("Audio system failed to initialize.");
    }
    _window->Show();

    // ImGui Window Manager context
    auto& windowRegistry = _registry.ctx().emplace<ImGuiWindowRegistry>();
    windowRegistry.RegisterWindow("Texture Debugger", false);
    windowRegistry.RegisterWindow("Audio Debugger", false);

    // Core systems
    _systems.push_back(std::make_unique<EcsDebugger>(_registry));
    _systems.push_back(std::make_unique<InputSystem>(_registry, inputEntity, _window.get()));
    _systems.push_back(std::make_unique<CameraSystem>(_registry, this));
    _systems.push_back(std::make_unique<CinematicCameraSystem>(_registry, this));
    _systems.push_back(std::make_unique<JoltPhysicsSystem>(_registry, this));

    //everything went fine
    _isInitialized = true;
}

void Core::InitPipelines()
{
    InitBackgroundPipelines();

    size_t maxInstances = 1000000; // upper bound
    _instanceBuffer = CreateBuffer(
        sizeof(InstanceData) * maxInstances,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    InitBRDFLUTPipeline();
    InitIrradiancePipeline();
    InitPrefilterPipeline();
    InitMeshPipeline();
    InitInstancedMeshPipeline();
    InitTransparentMeshPipeline();
    InitEffectMeshPipeline();
    InitShadowPipeline();
    InitLinePipeline();
    InitHeightFogPipeline();
    InitDepthVisualizationPipeline();
    InitScreenPostProcessMaskPipeline();
    InitScreenMaskBlurPipeline();
    InitScreenPostProcessPipeline();
    InitSelectionOutlinePipeline();
    InitSkyboxPipeline();
}

void Core::Run() {
    constexpr auto targetMinimizedFrameDuration = std::chrono::milliseconds(); // 20 FPS while minimized
    constexpr float fixedDelta = 1.0f / 60.0f; // 60 Hertz
    constexpr float maximumFrameDelta = 0.1f;
    constexpr int maximumFixedStepsPerFrame = 4;
    float fixedUpdateAccumulator = 0.0f;

    ENGINE_LOG_INFO("Starting Engine Main Loop.");
    bool running = true;
    std::vector<double> benchmarkFrameTimesMs;
    std::unordered_map<std::string, BenchmarkTiming> benchmarkSystemTimings;
    int benchmarkMeasuredFixedSteps = 0;
    int benchmarkFrameIndex = 0;
    if (_benchmarkOptions.enabled) {
        benchmarkFrameTimesMs.reserve(static_cast<std::size_t>(std::max(0, _benchmarkOptions.measuredFrames)));
        ENGINE_LOG_INFO(
            "Benchmark mode: warmup frames=" +
            std::to_string(_benchmarkOptions.warmupFrames) +
            ", measured frames=" +
            std::to_string(_benchmarkOptions.measuredFrames));
    }

    // initialize frame time
    auto previousFrameTime = std::chrono::high_resolution_clock::now();
    while (!_window->ShouldClose()) {
        auto frameStartTime = std::chrono::high_resolution_clock::now();
        const bool collectBenchmarkTimings =
            _benchmarkOptions.enabled &&
            benchmarkFrameIndex >= _benchmarkOptions.warmupFrames;
        std::chrono::duration<float> delta = frameStartTime - previousFrameTime;
        // Debugger breaks, resize stalls, GPU readbacks, and other blocking work
        // must not become simulation time on the following frame.
        _deltaTime = std::clamp(delta.count(), 0.0f, maximumFrameDelta);
        //set for next frame
        previousFrameTime = frameStartTime;

        // fixed update loop TODO verify if it should run before minimize check
        _window->PollEvents();
        fixedUpdateAccumulator = std::min(
            fixedUpdateAccumulator + _deltaTime,
            fixedDelta * static_cast<float>(maximumFixedStepsPerFrame));
        int fixedSteps = 0;
        while (fixedUpdateAccumulator >= fixedDelta &&
               fixedSteps < maximumFixedStepsPerFrame) {
            for (auto& system : _systems) {
                if (collectBenchmarkTimings) {
                    const auto systemStartTime = std::chrono::high_resolution_clock::now();
                    system->FixedUpdate(fixedDelta);
                    const auto systemEndTime = std::chrono::high_resolution_clock::now();
                    auto& timing = benchmarkSystemTimings[std::string{ "FixedUpdate " } + typeid(*system.get()).name()];
                    timing.totalMs += std::chrono::duration<double, std::milli>(
                        systemEndTime - systemStartTime).count();
                    ++timing.calls;
                }
                else {
                    system->FixedUpdate(fixedDelta);
                }
            }
            ResolveHierarchyTransforms(_registry);
            fixedUpdateAccumulator -= fixedDelta;
            ++fixedSteps;
            if (collectBenchmarkTimings) {
                ++benchmarkMeasuredFixedSteps;
            }
        }
        // update loop
        if (_window->WasResized()) {
            uint32_t width = _window->GetWidth();
            uint32_t height = _window->GetHeight();
            if ((_window->GetWidth() == 0 || _window->GetHeight() == 0) || glfwGetWindowAttrib(_window->GetNativeHandle(), GLFW_ICONIFIED)) {
                _appMinimized = true; // ignore swapchain
            }
            else {
                _appMinimized = false;
                // resize swapchain
                RecreateSwapchain(width, height);
            }
        }
        if (_appMinimized) {
            auto frameEndTime = std::chrono::high_resolution_clock::now();
            auto frameDuration = frameEndTime - frameStartTime;
            auto sleepDuration = targetMinimizedFrameDuration - frameDuration;

            if (sleepDuration > std::chrono::milliseconds(0))
            {
                std::this_thread::sleep_for(sleepDuration);
            }
            continue;
        }

        for (auto& system : _systems) {
            if (collectBenchmarkTimings) {
                const auto systemStartTime = std::chrono::high_resolution_clock::now();
                system->Update(_deltaTime);
                const auto systemEndTime = std::chrono::high_resolution_clock::now();
                auto& timing = benchmarkSystemTimings[std::string{ "Update " } + typeid(*system.get()).name()];
                timing.totalMs += std::chrono::duration<double, std::milli>(
                    systemEndTime - systemStartTime).count();
                ++timing.calls;
            }
            else {
                system->Update(_deltaTime);
            }
        }
        _audioSystem.Update();
        ResolveHierarchyTransforms(_registry);

        // imgui new frame
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        HandleCaptureShortcuts(_registry);
        const bool captureUiHidden = GetEditorCaptureState(_registry).uiHidden;
        if (!captureUiHidden) {
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
            DrawUi();
        }
        auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
        bool open = windowRegistry.IsWindowOpen("Texture Debugger");
        if (!captureUiHidden && open)
        {
            if (ImGui::Begin("Texture Debugger", &open))
            {
                const auto drawDebugTexture =
                    [&](const char* label, AllocatedImage& image, const char* exportName) {
                    if (image.imageView == VK_NULL_HANDLE) {
                        ImGui::TextDisabled("%s unavailable", label);
                        return;
                    }

                    if (image.imguiDescriptorSet == VK_NULL_HANDLE) {
                        image.imguiDescriptorSet = ImGui_ImplVulkan_AddTexture(
                            _defaultSamplerLinear,
                            image.imageView,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    }

                    ImGui::TextUnformatted(label);
                    ImGui::Text(
                        "%ux%u",
                        image.imageExtent.width,
                        image.imageExtent.height);

                    ImTextureID textureId =
                        reinterpret_cast<ImTextureID>(image.imguiDescriptorSet);
                    if (textureId) {
                        const float previewWidth = 256.0f;
                        const float aspect = image.imageExtent.height > 0
                            ? static_cast<float>(image.imageExtent.width) /
                                static_cast<float>(image.imageExtent.height)
                            : 1.0f;
                        ImGui::Image(
                            textureId,
                            ImVec2(previewWidth, previewWidth / glm::max(aspect, 0.001f)));
                    }
                    else {
                        ImGui::TextDisabled("Missing ImGui descriptor");
                    }

                    ImGui::PushID(label);
                    if (ImGui::Button("Export PNG")) {
                        const std::filesystem::path outputPath =
                            _projectRoot / "debug_capture" / exportName;
                        if (ExportR8DebugImagePng(image, outputPath)) {
                            ENGINE_LOG_INFO("Wrote texture debug capture: " + outputPath.generic_string());
                        }
                        else {
                            ENGINE_LOG_ERROR("Failed texture debug capture: " + outputPath.generic_string());
                        }
                    }
                    ImGui::PopID();
                };

                ImGui::Text("Shadow Map");
                ImGui::Text(
                    "%ux%u",
                    _shadowMapExtent.width,
                    _shadowMapExtent.height
                );

                ImTextureID shadowId =
                    reinterpret_cast<ImTextureID>(_shadowMapImage.imguiDescriptorSet);

                if (shadowId) {
                    ImGui::Image(shadowId, ImVec2(256.0f, 256.0f));
                }
                else {
                    ImGui::TextDisabled("Missing shadow map ImGui descriptor");
                }

                const bool capturePending =
                    _debugCaptureRequested ||
                    _depthCaptureRequested ||
                    _pendingScreenshot.buffer != VK_NULL_HANDLE ||
                    _pendingShadowMapCapture.buffer != VK_NULL_HANDLE;
                if (capturePending) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Export current view + shadow map")) {
                    _debugCaptureRequested = true;
                    ENGINE_LOG_INFO("Debug capture requested.");
                }
                if (ImGui::Button("Export camera depth")) {
                    _depthCaptureRequested = true;
                    ENGINE_LOG_INFO("Depth capture requested.");
                }
                if (capturePending) {
                    ImGui::EndDisabled();
                }
                ImGui::TextDisabled("Writes to the project-root debug_capture folder");

                ImGui::Separator();
                ImGui::TextUnformatted("Render Textures");
                if (_screenPostProcessDebugImagesReady) {
                    drawDebugTexture(
                        "Screen Post Process Source Mask",
                        _screenPostProcessMaskImage,
                        "screen_post_process_source_mask.png");
                    drawDebugTexture(
                        "Screen Post Process Blur Temp",
                        _screenPostProcessBlurTempImage,
                        "screen_post_process_blur_temp.png");
                    drawDebugTexture(
                        "Screen Post Process Blurred Mask",
                        _screenPostProcessBlurredMaskImage,
                        "screen_post_process_blurred_mask.png");
                }
                else {
                    ImGui::TextDisabled("No localized screen post-process mask has been rendered yet.");
                }

                ImGui::Separator();
                ImGui::TextUnformatted("IBL Maps");
                if (ImGui::Button("Export irradiance/prefilter PNGs")) {
                    ExportIblDebugPngs();
                }
                ImGui::TextDisabled("Writes six-face atlases to ibl_exports/");

                ImGui::Separator();

                for (size_t i = 0; i < _loadedImages.size(); i++)
                {
                    auto& image = _loadedImages[i];

                    if (!image)
                    {
                        ImGui::TextDisabled("Texture %zu : null", i);
                        continue;
                    }

                    ImGui::Text("Texture %zu", i);

                    ImTextureID id =
                        reinterpret_cast<ImTextureID>(image->imguiDescriptorSet);

                    if (!id)
                    {
                        ImGui::TextDisabled("Missing ImGui descriptor");
                        continue;
                    }

                    ImGui::Image(id, ImVec2(128.0f, 128.0f));
                }
            }

            ImGui::End();

            windowRegistry.SetWindowOpen("Texture Debugger", open);
        }

        bool audioOpen = windowRegistry.IsWindowOpen("Audio Debugger");
        if (audioOpen)
        {
            if (ImGui::Begin("Audio Debugger", &audioOpen))
            {
                ImGui::Text(
                    "OpenAL: %s",
                    _audioSystem.IsInitialized() ? "initialized" : "unavailable");

                if (!_audioSystem.IsInitialized()) {
                    ImGui::BeginDisabled();
                }

                if (ImGui::Button("Play test tone")) {
                    _audioSystem.PlayTestTone();
                }

                if (!_audioSystem.IsInitialized()) {
                    ImGui::EndDisabled();
                }
            }

            ImGui::End();
            windowRegistry.SetWindowOpen("Audio Debugger", audioOpen);
        }

        ImGui::Render();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        UpdateEnvironmentSkybox();
        
        Draw();

        if (_benchmarkOptions.enabled) {
            const auto benchmarkFrameEndTime = std::chrono::high_resolution_clock::now();
            const double frameTimeMs =
                std::chrono::duration<double, std::milli>(benchmarkFrameEndTime - frameStartTime).count();

            if (benchmarkFrameIndex >= _benchmarkOptions.warmupFrames) {
                benchmarkFrameTimesMs.push_back(frameTimeMs);
            }

            ++benchmarkFrameIndex;

            if (static_cast<int>(benchmarkFrameTimesMs.size()) >= _benchmarkOptions.measuredFrames) {
                auto sortedFrameTimes = benchmarkFrameTimesMs;
                std::sort(sortedFrameTimes.begin(), sortedFrameTimes.end());

                const double totalMs =
                    std::accumulate(benchmarkFrameTimesMs.begin(), benchmarkFrameTimesMs.end(), 0.0);
                const double averageMs = totalMs / static_cast<double>(benchmarkFrameTimesMs.size());
                const double minMs = sortedFrameTimes.front();
                const double maxMs = sortedFrameTimes.back();
                const std::size_t p95Index = static_cast<std::size_t>(
                    std::clamp(
                        static_cast<int>(std::ceil(static_cast<double>(sortedFrameTimes.size()) * 0.95)) - 1,
                        0,
                        static_cast<int>(sortedFrameTimes.size()) - 1));
                const double p95Ms = sortedFrameTimes[p95Index];
                const double averageFps = averageMs > 0.0 ? 1000.0 / averageMs : 0.0;

                std::ostringstream result;
                result << "Benchmark result: frames=" << benchmarkFrameTimesMs.size()
                       << ", avg_ms=" << averageMs
                       << ", min_ms=" << minMs
                       << ", p95_ms=" << p95Ms
                       << ", max_ms=" << maxMs
                       << ", avg_fps=" << averageFps;
                ENGINE_LOG_INFO(result.str());

                std::vector<std::pair<std::string, BenchmarkTiming>> sortedTimings {
                    benchmarkSystemTimings.begin(),
                    benchmarkSystemTimings.end()
                };
                std::sort(
                    sortedTimings.begin(),
                    sortedTimings.end(),
                    [](const auto& first, const auto& second) {
                        return first.second.totalMs > second.second.totalMs;
                    });

                std::ostringstream timingResult;
                timingResult << "Benchmark fixed steps during measured frames=" << benchmarkMeasuredFixedSteps;
                ENGINE_LOG_INFO(timingResult.str());

                const std::size_t timingCount = std::min<std::size_t>(8, sortedTimings.size());
                for (std::size_t index = 0; index < timingCount; ++index) {
                    const auto& [name, timing] = sortedTimings[index];
                    const double averageCallMs = timing.calls > 0
                        ? timing.totalMs / static_cast<double>(timing.calls)
                        : 0.0;

                    std::ostringstream systemResult;
                    systemResult << "Benchmark system " << name
                                 << ": total_ms=" << timing.totalMs
                                 << ", calls=" << timing.calls
                                 << ", avg_call_ms=" << averageCallMs;
                    ENGINE_LOG_INFO(systemResult.str());
                }
                break;
            }
        }
    }
    ENGINE_LOG_INFO("Engine Shutting Down.");
    Shutdown();
}

void Core::SetBenchmarkOptions(BenchmarkOptions options)
{
    options.warmupFrames = std::max(0, options.warmupFrames);
    options.measuredFrames = std::max(1, options.measuredFrames);
    _benchmarkOptions = options;
}

void Core::SetVSyncEnabled(bool enabled)
{
    _vsyncEnabled = enabled;
}

bool Core::SetSkyboxPath(const std::filesystem::path& projectPath)
{
    const std::string pathString = projectPath.generic_string();
    if (pathString.empty() || pathString == _activeSkyboxPath) {
        return true;
    }

    const std::array<std::string, 6> faceNames {
        "right.png",
        "left.png",
        "top.png",
        "bottom.png",
        "front.png",
        "back.png"
    };

    std::array<std::filesystem::path, 6> facePaths{};
    for (std::size_t index = 0; index < faceNames.size(); ++index) {
        facePaths[index] = projectPath / faceNames[index];
        if (!std::filesystem::is_regular_file(ResolveProjectPath(facePaths[index]))) {
            ENGINE_LOG_ERROR("Skybox folder missing face: " + facePaths[index].generic_string());
            return false;
        }
    }

    try {
        AllocatedImage newSkyboxImage = CreateCubemap(facePaths, true);
        newSkyboxImage.sampler = _skyboxCubemap.sampler;

        VK_CHECK(vkDeviceWaitIdle(_device));
        if (_skyboxCubemap.image.image != VK_NULL_HANDLE) {
            DestroyImage(_skyboxCubemap.image);
        }

        _skyboxCubemap.image = newSkyboxImage;
        _skyboxCubemap.mipLevels = newSkyboxImage.mipLevels;

        if (_skyboxCubemap.descriptorSet == VK_NULL_HANDLE) {
            _skyboxCubemap.descriptorSet =
                globalDescriptorAllocator.Allocate(_device, _skyboxDescriptorLayout);
        }

        DescriptorWriter skyboxWriter;
        skyboxWriter.write_image(
            0,
            _skyboxCubemap.image.imageView,
            _skyboxCubemap.sampler,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        skyboxWriter.update_set(_device, _skyboxCubemap.descriptorSet);

        _activeSkyboxPath = pathString;
        _failedSkyboxPath.clear();
        ENGINE_LOG_INFO("Loaded skybox: " + pathString);
        return true;
    }
    catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Failed to load skybox " + pathString + ": " + exception.what());
        return false;
    }
}

void Core::UpdateEnvironmentSkybox()
{
    auto environmentView = _registry.view<EnvironmentComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : environmentView) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        const auto& environment = environmentView.get<EnvironmentComponent>(entity);
        if (environment.skyboxPath.empty() ||
            environment.skyboxPath == _activeSkyboxPath ||
            environment.skyboxPath == _failedSkyboxPath) {
            return;
        }

        if (!SetSkyboxPath(environment.skyboxPath)) {
            _failedSkyboxPath = environment.skyboxPath;
        }
        return;
    }
}

entt::entity Core::ResolveRenderCameraEntity()
{
    if (IsPlayMode()) {
        auto playCameraView =
            _registry.view<Camera, Transform, ActiveCameraTag>(entt::exclude<CoreOwnedTag, DisabledEntityTag>);

        for (auto entity : playCameraView) {
            if (!IsEntityDisabled(_registry, entity)) {
                return entity;
            }
        }

        return entt::null;
    }

    auto pilotCameraView =
        _registry.view<Camera, Transform, EditorCameraPilotTag>(entt::exclude<CoreOwnedTag, DisabledEntityTag>);

    for (auto entity : pilotCameraView) {
        if (!IsEntityDisabled(_registry, entity)) {
            return entity;
        }
    }

    auto editorCameraView = _registry.view<Camera, Transform, ActiveCameraTag, CoreOwnedTag>();
    if (editorCameraView.begin() != editorCameraView.end()) {
        return *editorCameraView.begin();
    }

    auto sceneCameraView =
        _registry.view<Camera, Transform, ActiveCameraTag>(entt::exclude<CoreOwnedTag, DisabledEntityTag>);

    for (auto entity : sceneCameraView) {
        if (!IsEntityDisabled(_registry, entity)) {
            return entity;
        }
    }

    return entt::null;
}

void Core::Draw()
{
    auto cameraEntity = ResolveRenderCameraEntity();
    Camera* camera = nullptr;
    if (cameraEntity != entt::null && _registry.valid(cameraEntity)) {
        camera = _registry.try_get<Camera>(cameraEntity);
    }

    FrameData& frameData = GetCurrentFrame();
    //wait until the gpu has finished rendering the last frame. Timeout of 1 second
    VK_CHECK(vkWaitForFences(_device, 1, &GetCurrentFrame()._renderFence, true, 1000000000));

    // After vkWaitForFences, before anything else:
    if (_pendingScreenshot.buffer != VK_NULL_HANDLE) // todo move out
    {
        vkDeviceWaitIdle(_device);
        void* data;
        vmaMapMemory(_allocator, _pendingScreenshot.allocation, &data);

        uint16_t* src = (uint16_t*)data;
        uint32_t w = _pendingScreenshotExtent.width;
        uint32_t h = _pendingScreenshotExtent.height;

        // Convert R16G16B16A16_SFLOAT -> R8G8B8A8
        std::vector<uint8_t> pixels(w * h * 4);
        for (uint32_t i = 0; i < w * h * 4; i++)
        {
            // Proper float16 -> float32 conversion
            uint16_t h16 = src[i];
            uint32_t sign     = (h16 >> 15) & 0x1;
            uint32_t exponent = (h16 >> 10) & 0x1F;
            uint32_t mantissa = h16 & 0x3FF;
            
            float f;
            if (exponent == 0) {
                f = std::ldexp((float)mantissa, -24); // denormal
            } else if (exponent == 31) {
                f = 0.0f; // treat Inf and NaN as 0 instead of garbage
            } else {
                f = std::ldexp((float)(mantissa | 0x400), (int)exponent - 25);
            }

            if (sign) f = -f;

            // Guard against any remaining NaN/Inf slipping through
            if (!std::isfinite(f)) f = 0.0f;

            pixels[i] = (uint8_t)(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        std::filesystem::path screenshotPath = _projectRoot / "screenshot.png";
        if (_pendingScreenshotIsDepth) {
            screenshotPath = _projectRoot / "debug_capture/depth.png";
        }
        else if (_pendingShadowMapCapture.buffer != VK_NULL_HANDLE) {
            screenshotPath = _projectRoot / "debug_capture/view.png";
        }
        if (!screenshotPath.parent_path().empty()) {
            std::filesystem::create_directories(screenshotPath.parent_path());
        }
        const std::string screenshotPathString = screenshotPath.generic_string();
        const bool screenshotWritten = stbi_write_png(
            screenshotPathString.c_str(), w, h, 4, pixels.data(), w * 4) != 0;
        if (screenshotWritten) {
            ENGINE_LOG_INFO("Wrote view capture: " + screenshotPathString);
        }
        else {
            ENGINE_LOG_ERROR("Failed to write view capture: " + screenshotPathString);
        }

        vmaUnmapMemory(_allocator, _pendingScreenshot.allocation);
        DestroyBuffer(_pendingScreenshot);
        _pendingScreenshot.buffer = VK_NULL_HANDLE;
        _pendingScreenshotIsDepth = false;
    }

    if (_pendingShadowMapCapture.buffer != VK_NULL_HANDLE)
    {
        void* data = nullptr;
        vmaMapMemory(_allocator, _pendingShadowMapCapture.allocation, &data);

        const uint32_t w = _shadowMapExtent.width;
        const uint32_t h = _shadowMapExtent.height;
        const auto* depth = static_cast<const float*>(data);
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h);
        for (size_t i = 0; i < pixels.size(); ++i) {
            const float value = std::isfinite(depth[i])
                ? std::clamp(depth[i], 0.0f, 1.0f)
                : 0.0f;
            pixels[i] = static_cast<uint8_t>(value * 255.0f + 0.5f);
        }

        const std::filesystem::path shadowPath =
            _projectRoot / "debug_capture/shadow_map.png";
        std::error_code directoryError;
        std::filesystem::create_directories(shadowPath.parent_path(), directoryError);
        const std::string shadowPathString = shadowPath.generic_string();
        const bool shadowWritten = !directoryError && stbi_write_png(
            shadowPathString.c_str(),
            static_cast<int>(w),
            static_cast<int>(h),
            1,
            pixels.data(),
            static_cast<int>(w)) != 0;
        if (shadowWritten) {
            ENGINE_LOG_INFO("Wrote shadow-map capture: " + shadowPathString);
        }
        else {
            ENGINE_LOG_ERROR("Failed to write shadow-map capture: " + shadowPathString);
        }

        vmaUnmapMemory(_allocator, _pendingShadowMapCapture.allocation);
        DestroyBuffer(_pendingShadowMapCapture);
        _pendingShadowMapCapture.buffer = VK_NULL_HANDLE;
    }

    frameData._deletionQueue.flush();
    frameData._frameDescriptors.clear_pools(_device);
    VK_CHECK(vkResetFences(_device, 1, &frameData._renderFence));

    // // pick per frame semaphore
    VkSemaphore imageAvailableSemaphore = _imageAvailableSemaphores[_frameNumber % FRAME_OVERLAP];
    uint32_t swapchainImageIndex;
    VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
        imageAvailableSemaphore, nullptr, &swapchainImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        RecreateSwapchain(_window->GetWidth(), _window->GetHeight());
        result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
            imageAvailableSemaphore, nullptr, &swapchainImageIndex);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to recreate swapchain image on VK_ERROR_OUT_OF_DATE_KHR or VK_SUBOPTIMAL_KHR!");
        }
    }

    // -----------------------------------------------------------------------
    VkCommandBuffer cmd = GetCurrentFrame()._mainCommandBuffer;
    // begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo =
        vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    // start the command buffer recording
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    _drawExtent.width = _drawImage.imageExtent.width;
    _drawExtent.height = _drawImage.imageExtent.height;

    // transition our main draw image into general layout so we can write into it
    // we will overwrite it all so we dont care about what was the older layout
    DrawShadowMap(cmd);

    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    DrawBackground(cmd);
    
    //vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        vkutil::transition_image(cmd, _msaaColorImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vkutil::transition_image(cmd, _msaaDepthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    }
    else {
        vkutil::transition_image(cmd, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
    }
    DrawGeometry(cmd);
    DrawSelectedOutline(cmd);
    DrawHeightFog(cmd);
    DrawScreenPostProcess(cmd);
    { // TODO move to some other file
        const bool screenshotRequested = camera && camera->screenshotRequested;
        const bool depthCaptureRequested = _depthCaptureRequested;
        const bool captureDebugPair = _debugCaptureRequested || screenshotRequested;
        const bool captureAnyView = captureDebugPair || depthCaptureRequested;
        if (captureAnyView)
        {
            if (camera) {
                camera->screenshotRequested = false;
            }
            _debugCaptureRequested = false;
            _depthCaptureRequested = false;

            if (depthCaptureRequested) {
                DrawDepthVisualization(cmd);
            }

            // 1. Transition draw image to transfer source
            vkutil::transition_image(cmd, _drawImage.image,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            // Ensure all color attachment writes are visible before the transfer read
            VkMemoryBarrier2 memBarrier = {};
            memBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            memBarrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            memBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            memBarrier.dstStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

            VkDependencyInfo depInfo = {};
            depInfo.sType                = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.memoryBarrierCount   = 1;
            depInfo.pMemoryBarriers      = &memBarrier;

            vkCmdPipelineBarrier2(cmd, &depInfo);

            // 2. Create a host-visible staging buffer
            VkDeviceSize imageSize = _drawExtent.width * _drawExtent.height * 8; // RGBA16
            AllocatedBuffer stagingBuffer = CreateBuffer(imageSize,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY);

            // 3. Copy image to buffer
            VkBufferImageCopy copyRegion = {};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyRegion.imageSubresource.mipLevel = 0;
            copyRegion.imageSubresource.baseArrayLayer = 0;
            copyRegion.imageSubresource.layerCount = 1;
            copyRegion.imageExtent = { _drawExtent.width, _drawExtent.height, 1 };

            vkCmdCopyImageToBuffer(cmd, _drawImage.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                stagingBuffer.buffer, 1, &copyRegion);

            // 4. Transition back so the rest of the frame continues normally
            vkutil::transition_image(cmd, _drawImage.image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

            // 5. After command buffer submission + fence wait, map and save
            //    Queue this as a post-frame callback, or do it inline after vkWaitForFences next frame.
            //    Simplest: store the buffer and save after the fence signals.
            _pendingScreenshot = stagingBuffer; // store for readback
            _pendingScreenshotExtent = _drawExtent;
            _pendingScreenshotIsDepth = depthCaptureRequested;

            if (captureDebugPair) {
                const VkDeviceSize shadowMapSize =
                    static_cast<VkDeviceSize>(_shadowMapExtent.width) *
                    static_cast<VkDeviceSize>(_shadowMapExtent.height) *
                    sizeof(float);
                AllocatedBuffer shadowStagingBuffer = CreateBuffer(
                    shadowMapSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY);

                VkImageSubresourceRange depthRange =
                    vkinit::image_subresource_range(VK_IMAGE_ASPECT_DEPTH_BIT);
                vkutil::transition_image(
                    cmd,
                    _shadowMapImage.image,
                    VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    depthRange);

                VkBufferImageCopy shadowCopy{};
                shadowCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                shadowCopy.imageSubresource.mipLevel = 0;
                shadowCopy.imageSubresource.baseArrayLayer = 0;
                shadowCopy.imageSubresource.layerCount = 1;
                shadowCopy.imageExtent = {
                    _shadowMapExtent.width,
                    _shadowMapExtent.height,
                    1
                };
                vkCmdCopyImageToBuffer(
                    cmd,
                    _shadowMapImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    shadowStagingBuffer.buffer,
                    1,
                    &shadowCopy);

                vkutil::transition_image(
                    cmd,
                    _shadowMapImage.image,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                    depthRange);
                _pendingShadowMapCapture = shadowStagingBuffer;
            }
        }
        // record png image
    }

    //transition the draw image and the swapchain image into their correct transfer layouts
    vkutil::transition_image(cmd, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    //vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::transition_image(cmd,_swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    // execute a copy from the draw image into the swapchain
    vkutil::copy_image_to_image(cmd, _drawImage.image, _swapchainImages[swapchainImageIndex], _drawExtent, _swapchainExtent);

    // set swapchain image layout to Attachment Optimal so we can draw it
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    //draw imgui into the swapchain image
    DrawImGui(cmd,  _swapchainImageViews[swapchainImageIndex]);

    // set swapchain image layout to Present so we can draw it
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd));
    // ---------------------------------------------------------------------------------

    // prepare the submission to the queue. 
    // we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
    // we will signal the _renderSemaphore, to signal that rendering has finished

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);	
    
    VkSemaphore renderFinishedSemaphore = _renderFinishedSemaphores[swapchainImageIndex];
    VkSemaphoreSubmitInfo waitInfo =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        imageAvailableSemaphore);
    VkSemaphoreSubmitInfo signalInfo =
        vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        renderFinishedSemaphore); // always synchronize swapchain image with
                                                            // render semaphore	
    
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, &signalInfo, &waitInfo);	
    // submit command buffer to the queue and execute it.
    // _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, GetCurrentFrame()._renderFence));

    // prepare present
    // this will put the image we just rendered to into the visible window.
    // we want to wait on the _renderSemaphore for that, 
    // as its necessary that drawing commands have finished before the image is displayed to the user
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    //presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &_swapchain;
    presentInfo.pImageIndices = &swapchainImageIndex;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    //VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
    VkResult presentResult = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        int width = 0, height = 0;
        glfwGetFramebufferSize(_window->GetNativeHandle(), &width, &height);
        glfwWaitEvents();
        RecreateSwapchain(width, height);
    }
    else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image!");
    }
    // increase the number of frames drawn
    _frameNumber++;
}

void Core::DrawUi()
{
    for (auto& system : _systems) {
        system->DrawUi();
    }
}

void Core::DrawBackground(VkCommandBuffer cmd)
{
    // //make a clear-color from frame number. This will flash with a 120 frame period.
    VkClearColorValue clearValue {};
    clearValue.float32[0] = 0.1f;
    clearValue.float32[1] = 0.1f;
    clearValue.float32[2] = 0.5f;
    clearValue.float32[3] = 1.0f;

    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity != entt::null && _registry.valid(cameraEntity)) {
        auto* camera = _registry.try_get<Camera>(cameraEntity);

        if (camera && camera->backgroundMode == CameraBackgroundMode::SolidColor) {
            clearValue.float32[0] = camera->clearColor.r;
            clearValue.float32[1] = camera->clearColor.g;
            clearValue.float32[2] = camera->clearColor.b;
            clearValue.float32[3] = camera->clearColor.a;
        }
        else if (camera && camera->backgroundMode == CameraBackgroundMode::None) {
            clearValue.float32[0] = 0.0f;
            clearValue.float32[1] = 0.0f;
            clearValue.float32[2] = 0.0f;
            clearValue.float32[3] = 0.0f;
        }
    }

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
    
    // clear image
    vkCmdClearColorImage(cmd, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
}

void Core::DrawGeometry(VkCommandBuffer cmd)
{
    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity == entt::null || !_registry.valid(cameraEntity) || !_registry.all_of<Camera, Transform>(cameraEntity)) {
        return;
    }

    auto& renderCamera = _registry.get<Camera>(cameraEntity);
    auto& renderCameraTransform = _registry.get<Transform>(cameraEntity);
    glm::mat4 viewMatrix = renderCamera.GetViewMatrix(renderCameraTransform);
    const float renderAspectRatio =
        static_cast<float>(glm::max(_drawExtent.width, 1u)) /
        static_cast<float>(glm::max(_drawExtent.height, 1u));
    glm::mat4 projectionMatrix = renderCamera.GetProjectionMatrix(renderAspectRatio);

    //allocate a new uniform buffer for the scene data
    AllocatedBuffer gpuSceneDataBuffer = CreateBuffer(sizeof(GPUSceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    //add it to the deletion queue of this frame so it gets deleted once its been used
    GetCurrentFrame()._deletionQueue.push_function([=, this]() {
        DestroyBuffer(gpuSceneDataBuffer);
    });

    bool foundSunlight = false;
    auto sunlightView = _registry.view<SunlightComponent>(entt::exclude<DisabledEntityTag>);
    for (auto lightEntity : sunlightView) {
        if (IsEntityDisabled(_registry, lightEntity)) {
            continue;
        }

        auto& sunlight = sunlightView.get<SunlightComponent>(lightEntity);

        sceneData.sunlightDirection =
            glm::vec4(
                glm::normalize(sunlight.direction),
                sunlight.intensity
            );
        sceneData.ambientColor =
            glm::vec4(
                sunlight.color,
                sunlight.ambient
            );
        foundSunlight = true;
        break;
    }
    if (!foundSunlight) {
        sceneData.sunlightDirection = glm::vec4(0.0f);
        sceneData.ambientColor = glm::vec4(0.0f);
    }

    const auto isTransparentMaterial = [](const MaterialInstance* material) {
        return material && material->passType == MaterialPass::Transparent;
    };

    sceneData.cameraPosition = glm::vec4(renderCameraTransform.position, 1.0f);
    sceneData.lightViewProjection = _sunLightViewProjection;

    auto corruptionTimeView = _registry.view<MeshCorruptionComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : corruptionTimeView) {
        auto& corruption = corruptionTimeView.get<MeshCorruptionComponent>(entity);
        corruption.age += _deltaTime * std::max(0.0f, corruption.speed);
    }

    //write the buffer
    GPUSceneData* sceneUniformData = (GPUSceneData*)gpuSceneDataBuffer.allocation->GetMappedData();
    *sceneUniformData = sceneData;

    //create a descriptor set that binds that buffer and update it
    VkDescriptorSet globalDescriptor = GetCurrentFrame()._frameDescriptors.allocate(_device, _gpuSceneDataDescriptorLayout);

    DescriptorWriter writer;
    writer.write_buffer(0, gpuSceneDataBuffer.buffer, sizeof(GPUSceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.update_set(_device, globalDescriptor);

    VkClearValue colorClear{};
    colorClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    if (renderCamera.backgroundMode == CameraBackgroundMode::SolidColor) {
        colorClear.color.float32[0] = renderCamera.clearColor.r;
        colorClear.color.float32[1] = renderCamera.clearColor.g;
        colorClear.color.float32[2] = renderCamera.clearColor.b;
        colorClear.color.float32[3] = renderCamera.clearColor.a;
    }

    VkRenderingAttachmentInfo colorAttachment =
        _msaaSamples != VK_SAMPLE_COUNT_1_BIT
            ? vkinit::attachment_info(_msaaColorImage.imageView, &colorClear, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
            : vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    if (_msaaSamples != VK_SAMPLE_COUNT_1_BIT) {
        colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        colorAttachment.resolveImageView = _drawImage.imageView;
        colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAttachment =
        _msaaSamples != VK_SAMPLE_COUNT_1_BIT
            ? vkinit::depth_attachment_info(_msaaDepthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
            : vkinit::depth_attachment_info(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, &depthAttachment);
    
    vkCmdBeginRendering(cmd, &renderInfo);
    
    //set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0.f;
    viewport.maxDepth = 1.f;

    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetScissor(cmd, 0, 1, &scissor);

    if (renderCamera.backgroundMode == CameraBackgroundMode::Skybox) {
        vkCmdBindPipeline(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _skyboxPipeline
        );

        glm::mat4 skyboxView = glm::mat4(glm::mat3(viewMatrix));
        glm::mat4 skyboxVP = projectionMatrix * skyboxView;

        vkCmdPushConstants(
            cmd,
            _skyboxPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(glm::mat4),
            &skyboxVP
        );

        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _skyboxPipelineLayout,
            0,
            1,
            &_skyboxCubemap.descriptorSet,
            0,
            nullptr
        );

        vkCmdDraw(cmd, 36, 1, 0, 0);
    }


    // ECS Batch Rendering
    {
        auto t0 = Clock::now();
        for (auto& [key, instances] : _batches) {
            instances.clear();
        }

        // exclude entities that are not meant for batch rendering
        auto registryView = _registry.view<MeshComponent, Transform>(entt::exclude<SingleRenderTag, EffectMeshComponent, DisabledEntityTag>);

        for (auto entity : registryView) {
            if (IsEntityDisabled(_registry, entity)) {
                continue;
            }

            auto& meshComponent = registryView.get<MeshComponent>(entity);
            auto& trans = registryView.get<Transform>(entity);

            if (!meshComponent.mesh || meshComponent.mesh->surfaces.empty())
                continue;

            auto& surface = meshComponent.mesh->surfaces[0];
            auto* material = ResolveMeshMaterial(meshComponent, surface);
            if (isTransparentMaterial(material)) {
                continue;
            }

            RenderPipelineId pipelineId = ResolveEditorWireframePipeline(_instancedMeshPipelineId);
            if (material && material->pipelines.instanced.IsValid()) {
                pipelineId =
                    _editorWireframeEnabled && material->pipelines.wireframeInstanced.IsValid()
                        ? material->pipelines.wireframeInstanced
                        : material->pipelines.instanced;
            }

            InstanceData instance{};
            instance.position = trans.position;
            instance.rotation = trans.rotation;
            instance.scale = trans.scale;
            instance.baseColorFactor = meshComponent.baseColorFactor;
            instance.flashColorAndAmount = ResolveMeshFlash(_registry, entity);
            instance.corruptionColorAndAmount = ResolveMeshCorruptionColor(_registry, entity);
            instance.corruptionParams = ResolveMeshCorruptionParams(_registry, entity);

            auto& batch = _batches[MeshBatchKey{
                .mesh = meshComponent.mesh.get(),
                .material = material,
                .pipelineId = pipelineId
            }];
            if (batch.capacity() == 0) {
                batch.reserve(128);
            }

            batch.push_back(instance);
        }

        for (auto batchIt = _batches.begin(); batchIt != _batches.end();)
        {
            if (batchIt->second.empty()) {
                batchIt = _batches.erase(batchIt);
            }
            else {
                ++batchIt;
            }
        }

        auto t1 = Clock::now();

        size_t offset = 0; // starting point in the instance buffer
        RenderPipelineId boundInstancedPipelineId;
        for (auto& [key, instances] : _batches) {
            auto* mesh = key.mesh;
            auto* material = key.material;
            if (!mesh || mesh->surfaces.empty() || instances.empty())
                continue;

            auto& surface = mesh->surfaces[0];
            const MaterialPipeline& instancedPipeline = GetRenderPipeline(key.pipelineId);
            if (key.pipelineId != boundInstancedPipelineId) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline.pipeline);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline.layout, 1, 1, &globalDescriptor, 0, nullptr);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline.layout, 2, 1, &_environmentDescriptorSet, 0, nullptr);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline.layout, 3, 1, &_shadowDescriptorSet, 0, nullptr);
                boundInstancedPipelineId = key.pipelineId;
            }

            AllocatedImage* baseColor = material ? material->image : nullptr;
            AllocatedImage* normal = material ? material->normalImage : nullptr;
            AllocatedImage* metallicRoughness = material ? material->metallicRoughnessImage : nullptr;
            AllocatedImage* occlusion = material ? material->occlusionImage : nullptr;
            AllocatedImage* emissive = material ? material->emissionImage : nullptr;

            if (!baseColor) baseColor = &_greyImage;
            if (!normal) normal = &_flatNormalImage;
            if (!metallicRoughness) metallicRoughness = &_defaultMetallicRoughnessImage;
            if (!occlusion) occlusion = &_whiteImage;
            if (!emissive) emissive = &_blackImage;

            VkDescriptorSet imageSet =
                GetCurrentFrame()._frameDescriptors.allocate(
                    _device,
                    _multiImageDescriptorLayout
                );

            DescriptorWriter imageWriter;
            imageWriter.write_image(
                0,
                baseColor->imageView,
                baseColor->sampler != VK_NULL_HANDLE ? baseColor->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                1,
                normal->imageView,
                normal->sampler != VK_NULL_HANDLE ? normal->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                2,
                metallicRoughness->imageView,
                metallicRoughness->sampler != VK_NULL_HANDLE ? metallicRoughness->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                3,
                occlusion->imageView,
                occlusion->sampler != VK_NULL_HANDLE ? occlusion->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.write_image(
                4,
                emissive->imageView,
                emissive->sampler != VK_NULL_HANDLE ? emissive->sampler : _defaultSamplerLinear,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
            );

            imageWriter.update_set(_device, imageSet);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, instancedPipeline.layout, 0, 1, &imageSet, 0, nullptr);

            size_t dataSize = instances.size() * sizeof(InstanceData);

            // Copy CPU-side instances into the persistently mapped GPU buffer
            memcpy(static_cast<char*>(_instanceBuffer.info.pMappedData) + offset,
                instances.data(),
                dataSize);

            auto t2 = Clock::now();

            // Save GPU device address for push constants
            VkDeviceAddress instanceAddress = _instanceBuffer.deviceAddress + offset;

            // Push constants per mesh
            BatchDrawPushConstants pc{};
            pc.viewProjection = projectionMatrix * viewMatrix;
            pc.baseColorFactor = material ? material->baseColorFactor : glm::vec4{ 1.0f };
            pc.vertexBuffer = mesh->meshBuffers.vertexBufferAddress;
            pc.instanceBuffer = instanceAddress;

            vkCmdPushConstants(cmd, instancedPipeline.layout,
                            VK_SHADER_STAGE_VERTEX_BIT,
                            0,
                            sizeof(pc),
                            &pc);

            // Bind index buffer for this mesh
            vkCmdBindIndexBuffer(cmd, mesh->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Draw all instances of this mesh
            vkCmdDrawIndexed(cmd,
                            surface.count,              // index count per mesh
                            instances.size(),           // number of instances
                            surface.startIndex,
                            0,
                            0);

            offset += dataSize; // move pointer for the next batch
            auto t3 = Clock::now();
            auto ms = [](auto a, auto b) {
                return std::chrono::duration<float, std::milli>(b - a).count();
            };
            //printf("Build: %.2f ms | Upload: %.2f ms | Record: %.2f ms\n",
            //ms(t0,t1), ms(t1,t2), ms(t2,t3));
        }
    }

    // ECS Singles rendering
    auto registryViewSingles = _registry.view<MeshComponent, Transform, SingleRenderTag>(entt::exclude<EffectMeshComponent, DisabledEntityTag>);
    RenderPipelineId boundMeshPipelineId;

    for (auto entity : registryViewSingles) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        auto& meshComponent = registryViewSingles.get<MeshComponent>(entity);
        auto& transformComponent = registryViewSingles.get<Transform>(entity);

        auto meshAssetPtr = meshComponent.mesh.get();
        if (!meshAssetPtr || meshAssetPtr->surfaces.empty())
            continue;

        auto& surface = meshAssetPtr->surfaces[0];
        auto* material = ResolveMeshMaterial(meshComponent, surface);
        if (isTransparentMaterial(material)) {
            continue;
        }

        RenderPipelineId pipelineId = ResolveEditorWireframePipeline(_meshPipelineId);
        if (material && material->pipelines.single.IsValid()) {
            pipelineId =
                _editorWireframeEnabled && material->pipelines.wireframeSingle.IsValid()
                    ? material->pipelines.wireframeSingle
                    : material->pipelines.single;
        }

        const MaterialPipeline& meshPipeline = GetRenderPipeline(pipelineId);
        if (pipelineId != boundMeshPipelineId) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 2, 1, &_environmentDescriptorSet, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 3, 1, &_shadowDescriptorSet, 0, nullptr);
            boundMeshPipelineId = pipelineId;
        }

        AllocatedImage* baseColor = material ? material->image : nullptr;
        AllocatedImage* normal = material ? material->normalImage : nullptr;
        AllocatedImage* metallicRoughness = material ? material->metallicRoughnessImage : nullptr;
        AllocatedImage* occlusion = material ? material->occlusionImage : nullptr;
        AllocatedImage* emissive = material ? material->emissionImage : nullptr;

        if (!baseColor) baseColor = &_greyImage;
        if (!normal) normal = &_flatNormalImage;
        if (!metallicRoughness) metallicRoughness = &_defaultMetallicRoughnessImage;
        if (!occlusion) occlusion = &_whiteImage;
        if (!emissive) emissive = &_blackImage;

        VkDescriptorSet imageSet =
            GetCurrentFrame()._frameDescriptors.allocate(
                _device,
                _multiImageDescriptorLayout
            );

        DescriptorWriter imageWriter;
        imageWriter.write_image(
            0,
            baseColor->imageView,
            baseColor->sampler != VK_NULL_HANDLE ? baseColor->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        imageWriter.write_image(
            1,
            normal->imageView,
            normal->sampler != VK_NULL_HANDLE ? normal->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        imageWriter.write_image(
            2,
            metallicRoughness->imageView,
            metallicRoughness->sampler != VK_NULL_HANDLE ? metallicRoughness->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        imageWriter.write_image(
            3,
            occlusion->imageView,
            occlusion->sampler != VK_NULL_HANDLE ? occlusion->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        imageWriter.write_image(
            4,
            emissive->imageView,
            emissive->sampler != VK_NULL_HANDLE ? emissive->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );

        imageWriter.update_set(_device, imageSet);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 0, 1, &imageSet, 0, nullptr); 
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 1, 1, &globalDescriptor, 0, nullptr);
        // T R S
        glm::mat4 T = glm::translate(glm::mat4(1.0f), transformComponent.position);
        glm::mat4 R = glm::mat4_cast(transformComponent.rotation);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), transformComponent.scale);
        glm::mat4 model = T * R * S;

        GPUDrawPushConstants push_constants;
        push_constants.vertexBuffer = meshAssetPtr->meshBuffers.vertexBufferAddress;
        push_constants.model = model;
        push_constants.viewProjection = projectionMatrix * viewMatrix;
        push_constants.baseColorFactor =
            (material ? material->baseColorFactor : glm::vec4{ 1.0f }) *
            meshComponent.baseColorFactor;
        push_constants.flashColorAndAmount = ResolveMeshFlash(_registry, entity);
        push_constants.corruptionColorAndAmount = ResolveMeshCorruptionColor(_registry, entity);
        push_constants.corruptionParams = ResolveMeshCorruptionParams(_registry, entity);

        vkCmdPushConstants(cmd, meshPipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &push_constants);

        vkCmdBindIndexBuffer(cmd, meshAssetPtr->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        
        vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
    }

    // Transparent meshes render after opaque geometry. This first pass draws them
    // individually so alpha composition is predictable enough for editor/game use.
    auto registryViewTransparent = _registry.view<MeshComponent, Transform>(entt::exclude<EffectMeshComponent, DisabledEntityTag>);
    RenderPipelineId boundTransparentPipelineId;

    for (auto entity : registryViewTransparent) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        auto& meshComponent = registryViewTransparent.get<MeshComponent>(entity);
        auto& transformComponent = registryViewTransparent.get<Transform>(entity);

        auto meshAssetPtr = meshComponent.mesh.get();
        if (!meshAssetPtr || meshAssetPtr->surfaces.empty()) {
            continue;
        }

        auto& surface = meshAssetPtr->surfaces[0];
        auto* material = ResolveMeshMaterial(meshComponent, surface);
        if (!isTransparentMaterial(material)) {
            continue;
        }

        RenderPipelineId pipelineId = ResolveEditorWireframePipeline(_transparentMeshPipelineId);
        if (material && material->pipelines.single.IsValid()) {
            pipelineId =
                _editorWireframeEnabled && material->pipelines.wireframeSingle.IsValid()
                    ? material->pipelines.wireframeSingle
                    : material->pipelines.single;
        }

        const MaterialPipeline& meshPipeline = GetRenderPipeline(pipelineId);
        if (pipelineId != boundTransparentPipelineId) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 2, 1, &_environmentDescriptorSet, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 3, 1, &_shadowDescriptorSet, 0, nullptr);
            boundTransparentPipelineId = pipelineId;
        }

        AllocatedImage* baseColor = material ? material->image : nullptr;
        AllocatedImage* normal = material ? material->normalImage : nullptr;
        AllocatedImage* metallicRoughness = material ? material->metallicRoughnessImage : nullptr;
        AllocatedImage* occlusion = material ? material->occlusionImage : nullptr;
        AllocatedImage* emissive = material ? material->emissionImage : nullptr;

        if (!baseColor) baseColor = &_greyImage;
        if (!normal) normal = &_flatNormalImage;
        if (!metallicRoughness) metallicRoughness = &_defaultMetallicRoughnessImage;
        if (!occlusion) occlusion = &_whiteImage;
        if (!emissive) emissive = &_blackImage;

        VkDescriptorSet imageSet =
            GetCurrentFrame()._frameDescriptors.allocate(
                _device,
                _multiImageDescriptorLayout
            );

        DescriptorWriter imageWriter;
        imageWriter.write_image(
            0,
            baseColor->imageView,
            baseColor->sampler != VK_NULL_HANDLE ? baseColor->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        imageWriter.write_image(
            1,
            normal->imageView,
            normal->sampler != VK_NULL_HANDLE ? normal->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        imageWriter.write_image(
            2,
            metallicRoughness->imageView,
            metallicRoughness->sampler != VK_NULL_HANDLE ? metallicRoughness->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        imageWriter.write_image(
            3,
            occlusion->imageView,
            occlusion->sampler != VK_NULL_HANDLE ? occlusion->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        imageWriter.write_image(
            4,
            emissive->imageView,
            emissive->sampler != VK_NULL_HANDLE ? emissive->sampler : _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        );
        imageWriter.update_set(_device, imageSet);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 0, 1, &imageSet, 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline.layout, 1, 1, &globalDescriptor, 0, nullptr);

        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), transformComponent.position) *
            glm::mat4_cast(transformComponent.rotation) *
            glm::scale(glm::mat4(1.0f), transformComponent.scale);

        GPUDrawPushConstants pushConstants{};
        pushConstants.vertexBuffer = meshAssetPtr->meshBuffers.vertexBufferAddress;
        pushConstants.model = model;
        pushConstants.viewProjection = projectionMatrix * viewMatrix;
        pushConstants.baseColorFactor =
            (material ? material->baseColorFactor : glm::vec4{ 1.0f }) *
            meshComponent.baseColorFactor;
        pushConstants.flashColorAndAmount = ResolveMeshFlash(_registry, entity);
        pushConstants.corruptionColorAndAmount = ResolveMeshCorruptionColor(_registry, entity);
        pushConstants.corruptionParams = ResolveMeshCorruptionParams(_registry, entity);

        vkCmdPushConstants(cmd, meshPipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);

        vkCmdBindIndexBuffer(cmd, meshAssetPtr->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
    }

    const glm::mat4 viewProjection = projectionMatrix * viewMatrix;
    DrawEffectMeshes(cmd, viewProjection, renderCameraTransform.position);
    DrawLines(cmd, viewProjection);

    vkCmdEndRendering(cmd);
}

void Core::DrawEffectMeshes(
    VkCommandBuffer cmd,
    const glm::mat4& viewProjection,
    const glm::vec3& cameraPosition)
{
    if (_effectMeshPipeline == VK_NULL_HANDLE || _effectMeshPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    std::vector<entt::entity> expiredEffects;
    auto effectView = _registry.view<MeshComponent, Transform, EffectMeshComponent>(entt::exclude<DisabledEntityTag>);
    if (effectView.begin() == effectView.end()) {
        return;
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _effectMeshPipeline);

    for (auto entity : effectView) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        auto& meshComponent = effectView.get<MeshComponent>(entity);
        auto& transformComponent = effectView.get<Transform>(entity);
        auto& effect = effectView.get<EffectMeshComponent>(entity);

        auto* meshAsset = meshComponent.mesh.get();
        if (!meshAsset || meshAsset->surfaces.empty()) {
            continue;
        }

        transformComponent.position += effect.velocity * _deltaTime;
        const float angularSpeed = glm::length(effect.angularVelocity);
        if (angularSpeed > 0.0001f) {
            transformComponent.rotation =
                glm::normalize(
                    glm::angleAxis(angularSpeed * _deltaTime, effect.angularVelocity / angularSpeed) *
                    transformComponent.rotation);
        }

        const float lifetime = glm::max(effect.lifetime, 0.0001f);
        const float normalizedAge = glm::clamp(effect.age / lifetime, 0.0f, 1.0f);
        const float animatedScale =
            glm::mix(effect.startScale, effect.endScale, normalizedAge);
        const float alphaMultiplier = 1.0f - normalizedAge;

        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), transformComponent.position) *
            glm::mat4_cast(transformComponent.rotation) *
            glm::scale(glm::mat4(1.0f), transformComponent.scale * animatedScale);

        EffectMeshPushConstants pushConstants{};
        pushConstants.viewProjection = viewProjection;
        pushConstants.model = model;
        pushConstants.color = effect.color * meshComponent.baseColorFactor;
        pushConstants.params = glm::vec4(
            effect.fresnelPower,
            effect.fresnelIntensity,
            effect.baseIntensity,
            alphaMultiplier);
        pushConstants.corruptionColor = effect.corruptionColor;
        pushConstants.corruptionParams = glm::vec4(
            effect.corruptionScale,
            effect.corruptionSoftness,
            effect.corruptionIntensity,
            effect.corruptionAmount);
        pushConstants.cameraPosition = glm::vec4(cameraPosition, effect.age);
        pushConstants.vertexBuffer = meshAsset->meshBuffers.vertexBufferAddress;

        vkCmdPushConstants(
            cmd,
            _effectMeshPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);

        vkCmdBindIndexBuffer(cmd, meshAsset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& surface : meshAsset->surfaces) {
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }

        effect.age += _deltaTime;
        if (effect.destroyOnComplete && effect.age >= effect.lifetime) {
            expiredEffects.push_back(entity);
        }
    }

    for (auto entity : expiredEffects) {
        if (_registry.valid(entity)) {
            _registry.destroy(entity);
        }
    }
}

glm::mat4 Core::BuildSunLightViewProjection()
{
    glm::vec3 lightDirection = glm::vec3(sceneData.sunlightDirection);
    if (glm::dot(lightDirection, lightDirection) < 0.0001f) {
        lightDirection = glm::vec3(-0.3f, 1.0f, -0.6f);
    }
    lightDirection = glm::normalize(lightDirection);

    glm::vec3 center{0.0f};

    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity != entt::null &&
        _registry.valid(cameraEntity) &&
        _registry.all_of<Camera, Transform>(cameraEntity)) {
        const auto& cameraTransform = _registry.get<Transform>(cameraEntity);
        const glm::vec3 cameraForward =
            glm::normalize(cameraTransform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));

        center =
            cameraTransform.position +
            cameraForward * (_shadowOrthoRadius * 0.35f);
    }

    const float radius = glm::max(_shadowOrthoRadius, 1.0f);
    float depthRadius = glm::max(_shadowDepthRadius, radius * 2.0f);

    auto meshView = _registry.view<MeshComponent, Transform>(entt::exclude<EffectMeshComponent, DisabledEntityTag>);
    for (auto entity : meshView) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        const auto& mesh = meshView.get<MeshComponent>(entity);
        if (!mesh.mesh || mesh.mesh->surfaces.empty()) {
            continue;
        }
        if (auto* material = ResolveMeshMaterial(mesh, mesh.mesh->surfaces[0]);
            material && material->passType == MaterialPass::Transparent) {
            continue;
        }

        const auto& transform = meshView.get<Transform>(entity);
        const glm::vec3 absScale = glm::abs(transform.scale);
        const float maxScale = glm::max(absScale.x, glm::max(absScale.y, absScale.z));
        const glm::vec3 scaledLocalCenter = mesh.mesh->boundsCenter * transform.scale;
        const glm::vec3 worldCenter =
            transform.position +
            transform.rotation * scaledLocalCenter;
        const float worldRadius = mesh.mesh->boundsRadius * maxScale;
        const float distanceAlongLight =
            glm::abs(glm::dot(worldCenter - center, lightDirection)) +
            worldRadius;

        depthRadius = glm::max(depthRadius, distanceAlongLight + 25.0f);
    }

    const glm::vec3 lightPosition = center + lightDirection * depthRadius;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    if (glm::abs(glm::dot(up, lightDirection)) > 0.95f) {
        up = glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::mat4 lightView = glm::lookAt(lightPosition, center, up);
    glm::mat4 lightProjection = glm::ortho(
        -radius,
        radius,
        -radius,
        radius,
        depthRadius * 2.0f,
        0.1f);
    lightProjection[1][1] *= -1.0f;

    return lightProjection * lightView;
}

void Core::DrawShadowMap(VkCommandBuffer cmd)
{
    if (_shadowPipeline == VK_NULL_HANDLE ||
        _shadowPipelineLayout == VK_NULL_HANDLE ||
        _shadowMapImage.image == VK_NULL_HANDLE) {
        return;
    }

    auto sunlightView = _registry.view<SunlightComponent>(entt::exclude<DisabledEntityTag>);
    for (auto lightEntity : sunlightView) {
        if (IsEntityDisabled(_registry, lightEntity)) {
            continue;
        }

        const auto& sunlight = sunlightView.get<SunlightComponent>(lightEntity);
        sceneData.sunlightDirection = glm::vec4(glm::normalize(sunlight.direction), sunlight.intensity);
        break;
    }

    _sunLightViewProjection = BuildSunLightViewProjection();

    VkImageSubresourceRange depthRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_DEPTH_BIT);
    vkutil::transition_image(
        cmd,
        _shadowMapImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        depthRange);

    VkRenderingAttachmentInfo depthAttachment =
        vkinit::depth_attachment_info(_shadowMapImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo =
        vkinit::rendering_info(_shadowMapExtent, nullptr, &depthAttachment);

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(_shadowMapExtent.width);
    viewport.height = static_cast<float>(_shadowMapExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _shadowMapExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowPipeline);

    auto meshView = _registry.view<MeshComponent, Transform>(entt::exclude<EffectMeshComponent, DisabledEntityTag>);
    for (auto entity : meshView) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        auto& meshComponent = meshView.get<MeshComponent>(entity);
        auto& transformComponent = meshView.get<Transform>(entity);

        auto meshAssetPtr = meshComponent.mesh.get();
        if (!meshAssetPtr || meshAssetPtr->surfaces.empty()) {
            continue;
        }
        if (auto* material = ResolveMeshMaterial(meshComponent, meshAssetPtr->surfaces[0]);
            material && material->passType == MaterialPass::Transparent) {
            continue;
        }

        glm::mat4 model =
            glm::translate(glm::mat4(1.0f), transformComponent.position) *
            glm::mat4_cast(transformComponent.rotation) *
            glm::scale(glm::mat4(1.0f), transformComponent.scale);

        ShadowDrawPushConstants pushConstants{};
        pushConstants.lightViewProjection = _sunLightViewProjection;
        pushConstants.model = model;
        pushConstants.vertexBuffer = meshAssetPtr->meshBuffers.vertexBufferAddress;

        vkCmdPushConstants(
            cmd,
            _shadowPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);

        vkCmdBindIndexBuffer(cmd, meshAssetPtr->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

        for (const auto& surface : meshAssetPtr->surfaces) {
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        _shadowMapImage.image,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        depthRange);
}

void Core::DrawLines(VkCommandBuffer cmd, const glm::mat4& viewProjection)
{
    if (_linePipeline == VK_NULL_HANDLE || _linePipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    std::vector<LineVertex> vertices;
    std::vector<entt::entity> expiredLines;

    auto lineView = _registry.view<LineComponent>(entt::exclude<DisabledEntityTag>);
    vertices.reserve(128);

    for (auto entity : lineView) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        auto& line = lineView.get<LineComponent>(entity);
        vertices.push_back(LineVertex{ line.start, 0.0f, line.color });
        vertices.push_back(LineVertex{ line.end, 0.0f, line.color });

        if (!line.persistent) {
            line.remainingTime -= _deltaTime;
            if (line.remainingTime <= 0.0f) {
                expiredLines.push_back(entity);
            }
        }
    }

    if (vertices.empty()) {
        for (auto entity : expiredLines) {
            if (_registry.valid(entity)) {
                _registry.destroy(entity);
            }
        }
        return;
    }

    const size_t bufferSize = vertices.size() * sizeof(LineVertex);
    AllocatedBuffer lineBuffer = CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU);

    memcpy(lineBuffer.info.pMappedData, vertices.data(), bufferSize);

    GetCurrentFrame()._deletionQueue.push_function([=, this]() {
        DestroyBuffer(lineBuffer);
    });

    LineDrawPushConstants pushConstants{};
    pushConstants.viewProjection = viewProjection;
    pushConstants.vertexBuffer = lineBuffer.deviceAddress;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _linePipeline);
    vkCmdPushConstants(
        cmd,
        _linePipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);

    vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);

    for (auto entity : expiredLines) {
        if (_registry.valid(entity)) {
            _registry.destroy(entity);
        }
    }
}

void Core::DrawSelectedOutline(VkCommandBuffer cmd)
{
    if (_selectionMaskPipeline == VK_NULL_HANDLE ||
        _selectionMaskPipelineLayout == VK_NULL_HANDLE ||
        _selectionOutlinePipeline == VK_NULL_HANDLE ||
        _selectionOutlinePipelineLayout == VK_NULL_HANDLE ||
        _selectionMaskImage.image == VK_NULL_HANDLE) {
        return;
    }

    std::vector<entt::entity> outlineEntities;

    if (!IsPlayMode() && _registry.ctx().contains<EditorSelection>()) {
        const auto& selection = _registry.ctx().get<EditorSelection>();
        if (selection.selectedEntity != entt::null &&
            _registry.valid(selection.selectedEntity) &&
            !IsEntityDisabled(_registry, selection.selectedEntity) &&
            _registry.all_of<MeshComponent, Transform>(selection.selectedEntity)) {
            outlineEntities.push_back(selection.selectedEntity);
        }
    }

    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity == entt::null ||
        !_registry.valid(cameraEntity) ||
        !_registry.all_of<Camera, Transform>(cameraEntity)) {
        return;
    }

    if (outlineEntities.empty()) {
        return;
    }

    const auto& renderCamera = _registry.get<Camera>(cameraEntity);
    const auto& renderCameraTransform = _registry.get<Transform>(cameraEntity);
    const float renderAspectRatio =
        static_cast<float>(glm::max(_drawExtent.width, 1u)) /
        static_cast<float>(glm::max(_drawExtent.height, 1u));
    const glm::mat4 viewProjection =
        renderCamera.GetProjectionMatrix(renderAspectRatio) *
        renderCamera.GetViewMatrix(renderCameraTransform);

    VkImageSubresourceRange colorRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    vkutil::transition_image(
        cmd,
        _selectionMaskImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        colorRange);

    VkClearValue maskClear{};
    maskClear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderingAttachmentInfo maskAttachment =
        vkinit::attachment_info(
            _selectionMaskImage.imageView,
            &maskClear,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingInfo maskRenderInfo =
        vkinit::rendering_info(_drawExtent, &maskAttachment, nullptr);

    vkCmdBeginRendering(cmd, &maskRenderInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(_drawExtent.width);
    viewport.height = static_cast<float>(_drawExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _drawExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _selectionMaskPipeline);

    for (auto entity : outlineEntities) {
        const auto& meshComponent = _registry.get<MeshComponent>(entity);
        const auto& transformComponent = _registry.get<Transform>(entity);
        auto* meshAsset = meshComponent.mesh.get();
        if (!meshAsset || meshAsset->surfaces.empty()) {
            continue;
        }

        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), transformComponent.position) *
            glm::mat4_cast(transformComponent.rotation) *
            glm::scale(glm::mat4(1.0f), transformComponent.scale);

        SelectionMaskPushConstants maskPushConstants{};
        maskPushConstants.viewProjection = viewProjection;
        maskPushConstants.model = model;
        maskPushConstants.vertexBuffer = meshAsset->meshBuffers.vertexBufferAddress;

        vkCmdPushConstants(
            cmd,
            _selectionMaskPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(maskPushConstants),
            &maskPushConstants);

        vkCmdBindIndexBuffer(cmd, meshAsset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& surface : meshAsset->surfaces) {
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }
    }

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        _selectionMaskImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        colorRange);

    VkDescriptorSet maskSet =
        GetCurrentFrame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);

    DescriptorWriter writer;
    writer.write_image(
        0,
        _selectionMaskImage.imageView,
        _defaultSamplerNearest,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, maskSet);

    VkRenderingAttachmentInfo outlineAttachment =
        vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingInfo outlineRenderInfo =
        vkinit::rendering_info(_drawExtent, &outlineAttachment, nullptr);

    vkCmdBeginRendering(cmd, &outlineRenderInfo);

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    SelectionOutlinePushConstants outlinePushConstants{};
    outlinePushConstants.color = glm::vec4(0.2f, 1.0f, 0.35f, 0.95f);
    outlinePushConstants.texelSize = glm::vec2(
        1.0f / static_cast<float>(_drawExtent.width),
        1.0f / static_cast<float>(_drawExtent.height));
    outlinePushConstants.thickness = 2.5f;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _selectionOutlinePipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _selectionOutlinePipelineLayout,
        0,
        1,
        &maskSet,
        0,
        nullptr);
    vkCmdPushConstants(
        cmd,
        _selectionOutlinePipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(outlinePushConstants),
        &outlinePushConstants);

    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);
}

void Core::DrawHeightFog(VkCommandBuffer cmd)
{
    if (_heightFogPipeline == VK_NULL_HANDLE ||
        _heightFogMsaaPipeline == VK_NULL_HANDLE ||
        _heightFogPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity == entt::null ||
        !_registry.valid(cameraEntity) ||
        !_registry.all_of<Camera, Transform>(cameraEntity)) {
        return;
    }

    entt::entity fogEntity = entt::null;
    auto fogView = _registry.view<Transform, HeightFogComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : fogView) {
        const auto& fog = fogView.get<HeightFogComponent>(entity);
        if (!IsEntityDisabled(_registry, entity) &&
            fog.enabled &&
            fog.density > 0.0f &&
            fog.maxOpacity > 0.0f) {
            fogEntity = entity;
            break;
        }
    }
    if (fogEntity == entt::null) {
        return;
    }

    const auto& camera = _registry.get<Camera>(cameraEntity);
    const auto& cameraTransform = _registry.get<Transform>(cameraEntity);
    const auto& fogTransform = fogView.get<Transform>(fogEntity);
    const auto& fog = fogView.get<HeightFogComponent>(fogEntity);

    const float renderAspectRatio =
        static_cast<float>(glm::max(_drawExtent.width, 1u)) /
        static_cast<float>(glm::max(_drawExtent.height, 1u));
    const glm::mat4 viewMatrix = camera.GetViewMatrix(cameraTransform);
    const glm::mat4 projectionMatrix = camera.GetProjectionMatrix(renderAspectRatio);

    HeightFogPushConstants pushConstants{};
    pushConstants.inverseViewProjection = glm::inverse(projectionMatrix * viewMatrix);
    pushConstants.cameraPosition = glm::vec4(cameraTransform.position, 1.0f);
    pushConstants.fogCenterAndRadius = glm::vec4(
        fogTransform.position,
        glm::max(0.001f, fog.planetRadius));
    pushConstants.fogColorAndDensity = glm::vec4(
        fog.color,
        glm::max(0.0f, fog.density));
    pushConstants.fogParams = glm::vec4(
        glm::max(0.001f, fog.height),
        glm::max(0.0f, fog.distanceFalloff),
        glm::clamp(fog.maxOpacity, 0.0f, 1.0f),
        fog.debugOverlay
            ? -glm::max(0.05f, glm::clamp(fog.maxOpacity, 0.0f, 1.0f))
            : 0.0f);
    pushConstants.skyFogParams = glm::vec4(
        glm::max(0.0f, fog.skyDensityMultiplier),
        glm::clamp(fog.skyMaxOpacity, 0.0f, 1.0f),
        0.0f,
        0.0f);

    const bool useMsaaDepth = _msaaSamples != VK_SAMPLE_COUNT_1_BIT;
    AllocatedImage& depthImage = useMsaaDepth ? _msaaDepthImage : _depthImage;
    const VkImageLayout depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    const VkImageLayout depthReadLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    const VkImageSubresourceRange depthRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_DEPTH_BIT);

    vkutil::transition_image(
        cmd,
        depthImage.image,
        depthAttachmentLayout,
        depthReadLayout,
        depthRange);

    VkDescriptorSet depthSet =
        GetCurrentFrame()._frameDescriptors.allocate(_device, _sampledImageDescriptorLayout);
    DescriptorWriter writer;
    writer.write_image(
        0,
        depthImage.imageView,
        VK_NULL_HANDLE,
        depthReadLayout,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    writer.update_set(_device, depthSet);

    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        useMsaaDepth ? _heightFogMsaaPipeline : _heightFogPipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _heightFogPipelineLayout,
        0,
        1,
        &depthSet,
        0,
        nullptr);
    vkCmdPushConstants(
        cmd,
        _heightFogPipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        depthImage.image,
        depthReadLayout,
        depthAttachmentLayout,
        depthRange);
}

void Core::DrawDepthVisualization(VkCommandBuffer cmd)
{
    if (_depthVisualizationPipeline == VK_NULL_HANDLE ||
        _depthVisualizationMsaaPipeline == VK_NULL_HANDLE ||
        _depthVisualizationPipelineLayout == VK_NULL_HANDLE) {
        return;
    }

    const bool useMsaaDepth = _msaaSamples != VK_SAMPLE_COUNT_1_BIT;
    AllocatedImage& depthImage = useMsaaDepth ? _msaaDepthImage : _depthImage;
    const VkImageLayout depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    const VkImageLayout depthReadLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    const VkImageSubresourceRange depthRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_DEPTH_BIT);

    vkutil::transition_image(
        cmd,
        depthImage.image,
        depthAttachmentLayout,
        depthReadLayout,
        depthRange);

    VkDescriptorSet depthSet =
        GetCurrentFrame()._frameDescriptors.allocate(_device, _sampledImageDescriptorLayout);
    DescriptorWriter writer;
    writer.write_image(
        0,
        depthImage.imageView,
        VK_NULL_HANDLE,
        depthReadLayout,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    writer.update_set(_device, depthSet);

    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(_drawImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(_drawExtent.width);
    viewport.height = static_cast<float>(_drawExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        useMsaaDepth ? _depthVisualizationMsaaPipeline : _depthVisualizationPipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _depthVisualizationPipelineLayout,
        0,
        1,
        &depthSet,
        0,
        nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        depthImage.image,
        depthReadLayout,
        depthAttachmentLayout,
        depthRange);
}

bool Core::DrawScreenPostProcessMask(VkCommandBuffer cmd, const glm::mat4& viewProjection)
{
    if (_screenPostProcessMaskPipeline == VK_NULL_HANDLE ||
        _screenPostProcessMaskPipelineLayout == VK_NULL_HANDLE ||
        _screenPostProcessMaskImage.image == VK_NULL_HANDLE) {
        return false;
    }

    VkImageSubresourceRange colorRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    vkutil::transition_image(
        cmd,
        _screenPostProcessMaskImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        colorRange);

    VkClearValue maskClear{};
    maskClear.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderingAttachmentInfo maskAttachment =
        vkinit::attachment_info(
            _screenPostProcessMaskImage.imageView,
            &maskClear,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    VkRenderingInfo maskRenderInfo =
        vkinit::rendering_info(_drawExtent, &maskAttachment, nullptr);

    vkCmdBeginRendering(cmd, &maskRenderInfo);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(_drawExtent.width);
    viewport.height = static_cast<float>(_drawExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _drawExtent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _screenPostProcessMaskPipeline);

    const auto drawMaskEntity =
        [&](entt::entity entity, const MeshComponent& meshComponent, const Transform& transformComponent, float worldRadiusExtension) {
        if (IsEntityDisabled(_registry, entity)) {
            return false;
        }

        auto* meshAsset = meshComponent.mesh.get();
        if (!meshAsset || meshAsset->surfaces.empty()) {
            return false;
        }

        const float baseRadius = glm::max(meshAsset->boundsRadius, 0.0001f);
        const float radiusScale =
            (baseRadius + glm::max(0.0f, worldRadiusExtension)) / baseRadius;
        const glm::mat4 model =
            glm::translate(glm::mat4(1.0f), transformComponent.position) *
            glm::mat4_cast(transformComponent.rotation) *
            glm::scale(glm::mat4(1.0f), transformComponent.scale * radiusScale);

        ScreenPostProcessMaskPushConstants pushConstants{};
        pushConstants.viewProjection = viewProjection;
        pushConstants.model = model;
        pushConstants.params = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        pushConstants.vertexBuffer = meshAsset->meshBuffers.vertexBufferAddress;

        vkCmdPushConstants(
            cmd,
            _screenPostProcessMaskPipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);

        vkCmdBindIndexBuffer(cmd, meshAsset->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
        for (const auto& surface : meshAsset->surfaces) {
            vkCmdDrawIndexed(cmd, surface.count, 1, surface.startIndex, 0, 0);
        }

        return true;
    };

    bool drewMaskSource = false;
    auto sourceView = _registry.view<ScreenPostProcessSourceComponent, MeshComponent, Transform>(entt::exclude<DisabledEntityTag>);
    for (auto entity : sourceView) {
        const auto& source = sourceView.get<ScreenPostProcessSourceComponent>(entity);
        if (!source.enabled) {
            continue;
        }

        drewMaskSource |= drawMaskEntity(
            entity,
            sourceView.get<MeshComponent>(entity),
            sourceView.get<Transform>(entity),
            source.worldRadiusExtension);
    }

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        _screenPostProcessMaskImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        colorRange);

    return drewMaskSource;
}

void Core::BlurScreenPostProcessMask(VkCommandBuffer cmd, float feather)
{
    if (_screenMaskBlurPipeline == VK_NULL_HANDLE ||
        _screenMaskBlurPipelineLayout == VK_NULL_HANDLE ||
        _screenPostProcessMaskImage.image == VK_NULL_HANDLE ||
        _screenPostProcessBlurTempImage.image == VK_NULL_HANDLE ||
        _screenPostProcessBlurredMaskImage.image == VK_NULL_HANDLE) {
        return;
    }

    const VkImageSubresourceRange colorRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);
    const float minDimension = static_cast<float>(
        glm::max(1u, glm::min(_drawExtent.width, _drawExtent.height)));
    const float radiusPixels = glm::clamp(
        glm::max(0.0f, feather) * minDimension,
        0.0f,
        128.0f);

    const auto drawBlurPass =
        [&](AllocatedImage& sourceImage, AllocatedImage& targetImage, const glm::vec2& direction) {
        vkutil::transition_image(
            cmd,
            targetImage.image,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            colorRange);

        VkDescriptorSet sourceSet =
            GetCurrentFrame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);
        DescriptorWriter writer;
        writer.write_image(
            0,
            sourceImage.imageView,
            _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.update_set(_device, sourceSet);

        VkRenderingAttachmentInfo colorAttachment =
            vkinit::attachment_info(
                targetImage.imageView,
                nullptr,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);

        vkCmdBeginRendering(cmd, &renderInfo);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(_drawExtent.width);
        viewport.height = static_cast<float>(_drawExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = _drawExtent;

        ScreenMaskBlurPushConstants pushConstants{};
        pushConstants.texelSizeAndRadius = glm::vec4(
            1.0f / static_cast<float>(glm::max(_drawExtent.width, 1u)),
            1.0f / static_cast<float>(glm::max(_drawExtent.height, 1u)),
            radiusPixels,
            0.0f);
        pushConstants.directionAndPad = glm::vec4(direction, 0.0f, 0.0f);

        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _screenMaskBlurPipeline);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _screenMaskBlurPipelineLayout,
            0,
            1,
            &sourceSet,
            0,
            nullptr);
        vkCmdPushConstants(
            cmd,
            _screenMaskBlurPipelineLayout,
            VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(pushConstants),
            &pushConstants);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRendering(cmd);

        vkutil::transition_image(
            cmd,
            targetImage.image,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            colorRange);
    };

    drawBlurPass(_screenPostProcessMaskImage, _screenPostProcessBlurTempImage, glm::vec2(1.0f, 0.0f));
    drawBlurPass(_screenPostProcessBlurTempImage, _screenPostProcessBlurredMaskImage, glm::vec2(0.0f, 1.0f));
    _screenPostProcessDebugImagesReady = true;
}

void Core::DrawScreenPostProcess(VkCommandBuffer cmd)
{
    if (_screenPostProcessPipeline == VK_NULL_HANDLE ||
        _screenPostProcessMsaaPipeline == VK_NULL_HANDLE ||
        _screenPostProcessPipelineLayout == VK_NULL_HANDLE ||
        _postProcessImage.imageView == VK_NULL_HANDLE) {
        return;
    }

    entt::entity effectEntity = entt::null;
    auto effectView = _registry.view<ScreenPostProcessComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : effectView) {
        auto& effect = effectView.get<ScreenPostProcessComponent>(entity);
        if (IsEntityDisabled(_registry, entity) || !effect.enabled) {
            continue;
        }

        effect.age += _deltaTime * glm::max(0.0f, effect.speed);

        if ((effect.debugOverlay || effect.amount > 0.0f) &&
            effectEntity == entt::null) {
            effectEntity = entity;
        }
    }
    if (effectEntity == entt::null) {
        return;
    }

    auto cameraEntity = ResolveRenderCameraEntity();
    if (cameraEntity == entt::null ||
        !_registry.valid(cameraEntity) ||
        !_registry.all_of<Camera, Transform>(cameraEntity)) {
        return;
    }

    auto& effect = effectView.get<ScreenPostProcessComponent>(effectEntity);

    const auto& camera = _registry.get<Camera>(cameraEntity);
    const auto& cameraTransform = _registry.get<Transform>(cameraEntity);
    const float renderAspectRatio =
        static_cast<float>(glm::max(_drawExtent.width, 1u)) /
        static_cast<float>(glm::max(_drawExtent.height, 1u));
    const glm::mat4 viewMatrix = camera.GetViewMatrix(cameraTransform);
    const glm::mat4 projectionMatrix = camera.GetProjectionMatrix(renderAspectRatio);
    const glm::mat4 viewProjection = projectionMatrix * viewMatrix;
    const bool hasMask = effect.useSourceMask
        ? DrawScreenPostProcessMask(cmd, viewProjection)
        : false;
    if (effect.useSourceMask && !hasMask) {
        return;
    }
    if (effect.useSourceMask && hasMask) {
        BlurScreenPostProcessMask(cmd, effect.maskFeather);
    }

    ScreenPostProcessPushConstants pushConstants{};
    pushConstants.inverseViewProjection = glm::inverse(viewProjection);
    pushConstants.viewProjection = viewProjection;
    pushConstants.colorAndAmount = glm::vec4(
        effect.color,
        glm::clamp(effect.amount, 0.0f, 1.0f));
    pushConstants.corruptionParams = glm::vec4(
        glm::max(0.001f, effect.scale),
        glm::clamp(effect.softness, 0.001f, 1.0f),
        glm::max(0.0f, effect.intensity),
        effect.age);
    pushConstants.distortionParams = glm::vec4(
        glm::max(0.0f, effect.displacement),
        glm::max(0.0f, effect.chromaticAberration),
        glm::max(0.0001f, effect.blockSize),
        effect.debugOverlay ? 1.0f : 0.0f);
    pushConstants.worldCenterAndRadius = glm::vec4(
        cameraTransform.position,
        0.0f);
    pushConstants.cameraRightAndFeather = glm::vec4(
        glm::normalize(cameraTransform.rotation * glm::vec3(1.0f, 0.0f, 0.0f)),
        glm::max(0.0f, effect.maskFeather));
    pushConstants.localizationParams = glm::vec4(
        renderAspectRatio,
        effect.useSourceMask && hasMask ? 1.0f : 0.0f,
        0.0f,
        0.0f);

    vkutil::transition_image(
        cmd,
        _drawImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    vkutil::transition_image(
        cmd,
        _postProcessImage.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    const bool useMsaaDepth = _msaaSamples != VK_SAMPLE_COUNT_1_BIT;
    AllocatedImage& depthImage = useMsaaDepth ? _msaaDepthImage : _depthImage;
    const VkImageSubresourceRange depthRange =
        vkinit::image_subresource_range(VK_IMAGE_ASPECT_DEPTH_BIT);
    vkutil::transition_image(
        cmd,
        depthImage.image,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        depthRange);

    VkDescriptorSet sceneSet =
        GetCurrentFrame()._frameDescriptors.allocate(_device, _screenPostProcessDescriptorLayout);
    DescriptorWriter writer;
    writer.write_image(
        0,
        _drawImage.imageView,
        _defaultSamplerLinear,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.write_image(
        1,
        depthImage.imageView,
        VK_NULL_HANDLE,
        VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    writer.write_image(
        2,
        effect.useSourceMask && hasMask
            ? _screenPostProcessBlurredMaskImage.imageView
            : _whiteImage.imageView,
        _defaultSamplerLinear,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(_device, sceneSet);

    VkRenderingAttachmentInfo colorAttachment =
        vkinit::attachment_info(_postProcessImage.imageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_drawExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        useMsaaDepth ? _screenPostProcessMsaaPipeline : _screenPostProcessPipeline);
    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        _screenPostProcessPipelineLayout,
        0,
        1,
        &sceneSet,
        0,
        nullptr);
    vkCmdPushConstants(
        cmd,
        _screenPostProcessPipelineLayout,
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(pushConstants),
        &pushConstants);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    vkCmdEndRendering(cmd);

    vkutil::transition_image(
        cmd,
        _postProcessImage.image,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkutil::transition_image(
        cmd,
        _drawImage.image,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkutil::copy_image_to_image(
        cmd,
        _postProcessImage.image,
        _drawImage.image,
        _drawExtent,
        _drawExtent);
    vkutil::transition_image(
        cmd,
        _drawImage.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void Core::DrawImGui(VkCommandBuffer cmd, VkImageView targetImageView)
{
    VkRenderingAttachmentInfo colorAttachment = vkinit::attachment_info(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderInfo = vkinit::rendering_info(_swapchainExtent, &colorAttachment, nullptr);

    vkCmdBeginRendering(cmd, &renderInfo);

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
}

void Core::ExportIblDebugPngs()
{
    const std::filesystem::path outputDir = "ibl_exports";
    std::filesystem::create_directories(outputDir);

    ExportCubemapMipAtlasPng(
        _irradianceCubemap,
        0,
        outputDir / "irradiance_atlas.png");

    for (uint32_t mip = 0; mip < _prefilteredCubemap.image.mipLevels; ++mip) {
        ExportCubemapMipAtlasPng(
            _prefilteredCubemap,
            mip,
            outputDir / ("prefilter_mip" + std::to_string(mip) + "_atlas.png"));
    }
}

bool Core::ExportR8DebugImagePng(
    AllocatedImage& image,
    const std::filesystem::path& outputPath,
    VkImageLayout currentLayout)
{
    if (image.image == VK_NULL_HANDLE ||
        image.imageFormat != VK_FORMAT_R8_UNORM ||
        image.imageExtent.width == 0 ||
        image.imageExtent.height == 0) {
        return false;
    }

    const uint32_t width = image.imageExtent.width;
    const uint32_t height = image.imageExtent.height;
    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(width) *
        static_cast<VkDeviceSize>(height);
    AllocatedBuffer stagingBuffer = CreateBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkImageSubresourceRange range =
            vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

        vkutil::transition_image(
            cmd,
            image.image,
            currentLayout,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            range);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = { width, height, 1 };

        vkCmdCopyImageToBuffer(
            cmd,
            image.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer.buffer,
            1,
            &copyRegion);

        vkutil::transition_image(
            cmd,
            image.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            currentLayout,
            range);
    });

    void* mappedData = nullptr;
    vmaMapMemory(_allocator, stagingBuffer.allocation, &mappedData);
    const auto* sourcePixels = static_cast<const uint8_t*>(mappedData);
    std::vector<uint8_t> pixels(sourcePixels, sourcePixels + imageSize);
    vmaUnmapMemory(_allocator, stagingBuffer.allocation);
    DestroyBuffer(stagingBuffer);

    std::error_code directoryError;
    std::filesystem::create_directories(outputPath.parent_path(), directoryError);
    if (directoryError) {
        return false;
    }

    const std::string output = outputPath.generic_string();
    return stbi_write_png(
        output.c_str(),
        static_cast<int>(width),
        static_cast<int>(height),
        1,
        pixels.data(),
        static_cast<int>(width)) != 0;
}

bool Core::ExportCubemapMipAtlasPng(
    const CubemapAsset& cubemap,
    uint32_t mipLevel,
    const std::filesystem::path& outputPath)
{
    const auto& image = cubemap.image;
    if (image.image == VK_NULL_HANDLE ||
        image.imageFormat != VK_FORMAT_R16G16B16A16_SFLOAT ||
        mipLevel >= image.mipLevels) {
        return false;
    }

    const uint32_t faceSize = std::max(1u, image.imageExtent.width >> mipLevel);
    constexpr uint32_t faceCount = 6;
    constexpr uint32_t channels = 4;
    constexpr uint32_t bytesPerChannel = 2;
    const VkDeviceSize faceBytes =
        static_cast<VkDeviceSize>(faceSize) *
        static_cast<VkDeviceSize>(faceSize) *
        channels *
        bytesPerChannel;
    const VkDeviceSize bufferSize = faceBytes * faceCount;

    AllocatedBuffer stagingBuffer = CreateBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    ImmediateSubmit([&](VkCommandBuffer cmd) {
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel = mipLevel;
        range.levelCount = 1;
        range.baseArrayLayer = 0;
        range.layerCount = faceCount;

        vkutil::transition_image(
            cmd,
            image.image,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            range);

        std::array<VkBufferImageCopy, faceCount> copyRegions{};
        for (uint32_t face = 0; face < faceCount; ++face) {
            auto& region = copyRegions[face];
            region.bufferOffset = faceBytes * face;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mipLevel;
            region.imageSubresource.baseArrayLayer = face;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { faceSize, faceSize, 1 };
        }

        vkCmdCopyImageToBuffer(
            cmd,
            image.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            stagingBuffer.buffer,
            static_cast<uint32_t>(copyRegions.size()),
            copyRegions.data());

        vkutil::transition_image(
            cmd,
            image.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            range);
    });

    void* mappedData = nullptr;
    vmaMapMemory(_allocator, stagingBuffer.allocation, &mappedData);

    const auto* src = static_cast<const uint16_t*>(mappedData);
    const uint32_t atlasWidth = faceSize * faceCount;
    const uint32_t atlasHeight = faceSize;
    std::vector<uint8_t> pixels(
        static_cast<size_t>(atlasWidth) *
        static_cast<size_t>(atlasHeight) *
        channels);

    const uint32_t srcFaceTexelCount = faceSize * faceSize;
    for (uint32_t face = 0; face < faceCount; ++face) {
        const uint32_t srcFaceOffset = face * srcFaceTexelCount * channels;
        for (uint32_t y = 0; y < faceSize; ++y) {
            for (uint32_t x = 0; x < faceSize; ++x) {
                const uint32_t srcIndex =
                    srcFaceOffset +
                    (y * faceSize + x) * channels;
                const uint32_t dstIndex =
                    (y * atlasWidth + face * faceSize + x) * channels;

                pixels[dstIndex + 0] = LinearFloatToPngByte(Float16ToFloat32(src[srcIndex + 0]));
                pixels[dstIndex + 1] = LinearFloatToPngByte(Float16ToFloat32(src[srcIndex + 1]));
                pixels[dstIndex + 2] = LinearFloatToPngByte(Float16ToFloat32(src[srcIndex + 2]));
                pixels[dstIndex + 3] = LinearFloatToPngByte(Float16ToFloat32(src[srcIndex + 3]));
            }
        }
    }

    vmaUnmapMemory(_allocator, stagingBuffer.allocation);
    DestroyBuffer(stagingBuffer);

    std::filesystem::create_directories(outputPath.parent_path());
    const std::string output = outputPath.generic_string();
    return stbi_write_png(
        output.c_str(),
        static_cast<int>(atlasWidth),
        static_cast<int>(atlasHeight),
        static_cast<int>(channels),
        pixels.data(),
        static_cast<int>(atlasWidth * channels)) != 0;
}

void Core::ImmediateSubmit(std::function<void(VkCommandBuffer cmd)> &&function)
{
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer cmd = _immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    function(cmd);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo, nullptr, nullptr);

    // submit command buffer to the queue and execute it.
    //  _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));

    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

void Core::Shutdown() { // todo move things out to deletion queues on creation if possible
    if (!_isInitialized)
        return;

    _audioSystem.Shutdown();

    _threadPool.~ThreadPool(); // destroy thread pool
    vkDeviceWaitIdle(_device);

    // 1 destroy per frame resources
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        // destroy command pool
        vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);
        // destroy fence
        vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
        // deletion queuq
        _frames[i]._deletionQueue.flush();
    }
    // 2 destroy semaphores
    for (int i = 0; i < FRAME_OVERLAP; i++) {
        vkDestroySemaphore(_device, _imageAvailableSemaphores[i], nullptr);
    }

    for (int i = 0; i < _renderFinishedSemaphores.size(); i++) {
        vkDestroySemaphore(_device, _renderFinishedSemaphores[i], nullptr);
    }

    // 3 destroy main deletion queue
    CleanupDrawImageDescriptors();
    CleanupDrawImages();
    DestroyBuffer(_instanceBuffer);
    _mainDeletionQueue.flush();

    // 4 destroy swapchain images
    CleanupSwapchainResources();

    // 5 destroy surface and device
    vkDestroySurfaceKHR(_instance, _surface, nullptr);
    vkDestroyDevice(_device, nullptr);

    // 6 destroy debug messenger
    vkb::destroy_debug_utils_messenger(_instance, _debugMessenger);
    vkDestroyInstance(_instance, nullptr);
    
    // 7 shutdown GLFW library
    _window->ShutdownGLFW();
}

entt::registry &Core::GetRegistry()
{
    return _registry;
}

// todo move to CoreThreads
ThreadPool::ThreadPool(size_t numThreads)
{
    threadCount = numThreads;
    for (size_t i = 0; i < numThreads; ++i) {
        workers.emplace_back([this]() {
            while (true) {
                std::function<void()> job;

                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock, [&]() { return stop || !jobs.empty(); });

                    if (stop && jobs.empty())
                        return;

                    job = std::move(jobs.front());
                    jobs.pop();
                    activeJobs++;
                }

                job();

                {
                    std::unique_lock lock(mutex);
                    activeJobs--;
                    if (jobs.empty() && activeJobs == 0)
                        doneCv.notify_one();
                }
            }
        });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
    }

    cv.notify_all();

    for (auto& worker : workers) {
        if (worker.joinable())
            worker.join();
    }
}

void ThreadPool::Enqueue(std::function<void()> job)
{
    {
        std::lock_guard lock(mutex);
        jobs.push(std::move(job));
    }
    cv.notify_one();
}

void ThreadPool::Wait()
{
    std::unique_lock lock(mutex);
    doneCv.wait(lock, [&]() {
        return jobs.empty() && activeJobs == 0;
    });
}

} // namespace engine
