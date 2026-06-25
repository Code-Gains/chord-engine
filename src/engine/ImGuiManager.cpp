#include "ImGuiManager.h"
#include "ImGuiWindowRegistry.h"
#include "Core.h"
#include "RuntimePauseState.h"
#include "WorldSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

ImGuiManager::ImGuiManager(entt::registry& registry)
    : System(registry)
{
}

ImGuiManager::ImGuiManager(entt::registry& registry, Engine::Core* core)
    : System(registry)
    , _core(core)
{
}

void ImGuiManager::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    EnsureDefaultWorldPath();

    if (_saveStatusTimer > 0.0f) {
        _saveStatusTimer = std::max(0.0f, _saveStatusTimer - ImGui::GetIO().DeltaTime);
    }

    if (_core != nullptr &&
        !_core->IsPlayMode() &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_N, false))
    {
        NewWorld();
    }

    if (_core != nullptr &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        SaveCurrentWorldOrOpenDialog();
    }

    if (_core != nullptr &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_O, false))
    {
        RequestWorldFileDialog(WorldFileDialogMode::Open);
    }

    if (!ImGui::GetIO().WantTextInput &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_H, false))
    {
        ToggleEditorWindows();
    }

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N", false, _core != nullptr && !_core->IsPlayMode())) {
                NewWorld();
            }
            if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, _core != nullptr)) {
                RequestWorldFileDialog(WorldFileDialogMode::Open);
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, _core != nullptr)) {
                SaveCurrentWorldOrOpenDialog();
            }
            if (ImGui::MenuItem("Save Scene As...", nullptr, false, _core != nullptr)) {
                RequestWorldFileDialog(WorldFileDialogMode::Save);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            if (ImGui::MenuItem(
                    windowRegistry.HasHiddenWindows() ? "Restore Editor Windows" : "Hide Editor Windows",
                    "Ctrl+Shift+H")) {
                ToggleEditorWindows();
            }
            ImGui::Separator();
            windowRegistry.DrawMenuItems();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Window Positions")) {
                ResetEditorWindowPositions();
            }
            ImGui::EndMenu();
        }

        DrawSceneMenuStatus();
        DrawPlayControls();
        ImGui::EndMainMenuBar();
    }

    if (_openWorldFileDialogRequested) {
        ImGui::OpenPopup("World File");
        _openWorldFileDialogRequested = false;
    }

    DrawWorldFileDialog();
}

void ImGuiManager::DrawPlayControls()
{
    if (_core == nullptr) {
        return;
    }

    const bool isPlayMode = _core->IsPlayMode();
    const bool isMenuPaused =
        Engine::HasPauseReason(_registry, Engine::RuntimePauseReason::Menu);

    ImGui::Separator();

    if (isPlayMode) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Play")) {
        Engine::ClearRuntimePause(_registry);
        _core->StartPlayMode();
    }

    if (isPlayMode) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (!isPlayMode) {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Stop")) {
        Engine::ClearRuntimePause(_registry);
        _core->StopPlayMode();
    }

    ImGui::SameLine();

    if (ImGui::Button(isMenuPaused ? "Resume" : "Pause")) {
        Engine::SetPauseReason(
            _registry,
            Engine::RuntimePauseReason::Menu,
            !isMenuPaused);
    }

    if (!isPlayMode) {
        ImGui::EndDisabled();
    }
}

void ImGuiManager::DrawSceneMenuStatus()
{
    ImGui::Separator();

    std::string sceneLabel = "Untitled";
    if (_core != nullptr && _core->GetCurrentWorldPath().has_value()) {
        sceneLabel = _core->GetCurrentWorldPath().value().generic_string();
    }

    ImGui::TextDisabled("Scene: %s", sceneLabel.c_str());

    if (_saveStatusTimer > 0.0f && !_saveStatusText.empty()) {
        ImGui::SameLine();
        const ImVec4 color = _lastSaveSucceeded
            ? ImVec4{ 0.35f, 0.85f, 0.45f, 1.0f }
            : ImVec4{ 1.0f, 0.35f, 0.25f, 1.0f };
        ImGui::TextColored(color, "%s", _saveStatusText.c_str());
    }
}

void ImGuiManager::RequestWorldFileDialog(WorldFileDialogMode mode)
{
    _worldFileDialogMode = mode;
    _overwriteConfirmationActive = false;
    _openWorldFileDialogRequested = true;
}

void ImGuiManager::NewWorld()
{
    if (_core == nullptr || _core->IsPlayMode()) {
        return;
    }

    auto serializer = _core->CreateWorldSerializer();
    const nlohmann::json emptyWorld = {
        {"version", Engine::Serialization::CurrentWorldVersion},
        {"entities", nlohmann::json::array()}
    };

    if (serializer.LoadWorldFromJson(*_core, emptyWorld)) {
        _core->ClearCurrentWorldPath();
        _worldFileDialogMode = WorldFileDialogMode::None;
        _overwriteConfirmationActive = false;
        _lastSaveSucceeded = true;
        _saveStatusText = "New scene";
        _saveStatusTimer = 2.0f;
        SetWorldPathBuffer("assets/worlds/editor_test.json");
    }
    else {
        _lastSaveSucceeded = false;
        _saveStatusText = "New scene failed";
        _saveStatusTimer = 4.0f;
    }
}

void ImGuiManager::SaveCurrentWorldOrOpenDialog()
{
    if (_core == nullptr) {
        return;
    }

    if (_core->GetCurrentWorldPath().has_value()) {
        SaveWorldToPath(_core->ResolveProjectPath(_core->GetCurrentWorldPath().value()));
        return;
    }

    RequestWorldFileDialog(WorldFileDialogMode::Save);
}

void ImGuiManager::EnsureDefaultWorldPath()
{
    if (_worldFileDialogMode == WorldFileDialogMode::None &&
        _core &&
        _core->GetCurrentWorldPath().has_value())
    {
        const auto currentPath = _core->GetCurrentWorldPath().value();
        if (std::string(_worldPathBuffer.data()) != currentPath.generic_string()) {
            SetWorldPathBuffer(currentPath);
        }
        return;
    }

    if (_worldPathBuffer[0] != '\0') {
        return;
    }

    constexpr std::string_view defaultWorldPath =
        "assets/worlds/editor_test.json";

    SetWorldPathBuffer(defaultWorldPath.data());
}

void ImGuiManager::SetWorldPathBuffer(const std::filesystem::path& path)
{
    const auto pathString = path.generic_string();
    std::fill(_worldPathBuffer.begin(), _worldPathBuffer.end(), '\0');
    std::strncpy(
        _worldPathBuffer.data(),
        pathString.c_str(),
        _worldPathBuffer.size() - 1
    );
}

void ImGuiManager::DrawWorldFileDialog()
{
    if (_core == nullptr) {
        return;
    }

    if (ImGui::BeginPopupModal("World File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const bool isOpenMode = _worldFileDialogMode == WorldFileDialogMode::Open;

        ImGui::TextUnformatted(isOpenMode ? "Open scene file" : "Save scene file");
        ImGui::SetNextItemWidth(460.0f);
        ImGui::InputText(
            "Path",
            _worldPathBuffer.data(),
            _worldPathBuffer.size()
        );
        if (ImGui::IsItemEdited()) {
            _overwriteConfirmationActive = false;
        }

        ImGui::TextDisabled("Project-relative paths are resolved from the project root.");

        auto worldPath = _core->ResolveProjectPath(_worldPathBuffer.data());

        if (!isOpenMode &&
            _overwriteConfirmationActive &&
            std::filesystem::exists(worldPath))
        {
            ImGui::Separator();
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                "This world already exists."
            );

            if (ImGui::Button("Overwrite")) {
                if (SaveWorldToPath(worldPath)) {
                    _core->SetCurrentWorldPath(_worldPathBuffer.data());
                    _worldFileDialogMode = WorldFileDialogMode::None;
                    _overwriteConfirmationActive = false;
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel Overwrite")) {
                _overwriteConfirmationActive = false;
            }
        } else if (ImGui::Button(isOpenMode ? "Open" : "Save")) {
            if (isOpenMode) {
                auto serializer = _core->CreateWorldSerializer();
                if (serializer.LoadWorld(*_core, worldPath)) {
                    _core->SetCurrentWorldPath(_worldPathBuffer.data());
                    _worldFileDialogMode = WorldFileDialogMode::None;
                    ImGui::CloseCurrentPopup();
                }
            } else {
                if (std::filesystem::exists(worldPath)) {
                    _overwriteConfirmationActive = true;
                } else if (SaveWorldToPath(worldPath)) {
                    _core->SetCurrentWorldPath(_worldPathBuffer.data());
                    _worldFileDialogMode = WorldFileDialogMode::None;
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            _worldFileDialogMode = WorldFileDialogMode::None;
            _overwriteConfirmationActive = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool ImGuiManager::SaveWorldToPath(const std::filesystem::path& worldPath)
{
    if (_core == nullptr) {
        return false;
    }

    std::filesystem::create_directories(worldPath.parent_path());
    auto serializer = _core->CreateWorldSerializer();
    const bool saved = serializer.SaveWorld(*_core, worldPath);

    _lastSaveSucceeded = saved;
    _saveStatusText = saved ? "Saved" : "Save failed";
    _saveStatusTimer = 2.0f;

    return saved;
}

void ImGuiManager::ResetEditorWindowPositions()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 basePosition = viewport
        ? ImVec2{ viewport->WorkPos.x + 24.0f, viewport->WorkPos.y + 48.0f }
        : ImVec2{ 24.0f, 48.0f };

    int index = 0;
    for (const auto& windowName : windowRegistry.WindowNames()) {
        const ImVec2 position {
            basePosition.x + static_cast<float>(index % 4) * 34.0f,
            basePosition.y + static_cast<float>(index % 4) * 34.0f
        };

        ImGui::SetWindowPos(windowName.c_str(), position, ImGuiCond_Always);
        ImGui::SetWindowCollapsed(windowName.c_str(), false, ImGuiCond_Always);
        windowRegistry.SetWindowOpen(windowName, true);
        ++index;
    }

    _lastSaveSucceeded = true;
    _saveStatusText = "Window positions reset";
    _saveStatusTimer = 2.0f;
}

void ImGuiManager::ToggleEditorWindows()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    const bool restoring = windowRegistry.HasHiddenWindows();
    windowRegistry.ToggleHiddenWindows();

    _lastSaveSucceeded = true;
    _saveStatusText = restoring ? "Editor windows restored" : "Editor windows hidden";
    _saveStatusTimer = 2.0f;
}
