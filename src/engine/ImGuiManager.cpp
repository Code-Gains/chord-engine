#include "ImGuiManager.h"
#include "ImGuiWindowRegistry.h"
#include "Core.h"
#include "WorldSerializer.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
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

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New", "Ctrl+N");
            if (ImGui::MenuItem("Open World...", "Ctrl+O", false, _core != nullptr)) {
                _worldFileDialogMode = WorldFileDialogMode::Open;
                _openWorldFileDialogRequested = true;
            }
            if (ImGui::MenuItem("Save World", "Ctrl+S", false, _core != nullptr)) {
                if (_core->GetCurrentWorldPath().has_value()) {
                    SaveWorldToPath(_core->ResolveProjectPath(_core->GetCurrentWorldPath().value()));
                } else {
                    _worldFileDialogMode = WorldFileDialogMode::Save;
                    _overwriteConfirmationActive = false;
                    _openWorldFileDialogRequested = true;
                }
            }
            if (ImGui::MenuItem("Save World As...", nullptr, false, _core != nullptr)) {
                _worldFileDialogMode = WorldFileDialogMode::Save;
                _overwriteConfirmationActive = false;
                _openWorldFileDialogRequested = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Game"))
        {
            const bool isPlayMode = _core != nullptr && _core->IsPlayMode();

            if (isPlayMode) {
                ImGui::BeginDisabled();
            }

            if (ImGui::MenuItem("Play", nullptr, false, _core != nullptr && !isPlayMode)) {
                _core->StartPlayMode();
            }

            if (isPlayMode) {
                ImGui::EndDisabled();
            }

            if (!isPlayMode) {
                ImGui::BeginDisabled();
            }

            if (ImGui::MenuItem("Stop", nullptr, false, _core != nullptr && isPlayMode)) {
                _core->StopPlayMode();
            }

            if (!isPlayMode) {
                ImGui::EndDisabled();
            }

            ImGui::Separator();
            ImGui::TextDisabled(isPlayMode ? "Mode: Play" : "Mode: Edit");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            windowRegistry.DrawMenuItems();
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (_openWorldFileDialogRequested) {
        ImGui::OpenPopup("World File");
        _openWorldFileDialogRequested = false;
    }

    DrawWorldFileDialog();
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

        ImGui::TextUnformatted(isOpenMode ? "Open world file" : "Save world file");
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
    return serializer.SaveWorld(*_core, worldPath);
}
