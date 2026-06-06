#pragma once
#include "System.h"
#include <array>
#include <entt/entt.hpp>
#include <filesystem>
#include <string>

namespace Engine {
class Core;
}

class ImGuiManager : public System {
    void Update(float deltaTime) override {};
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

public:
    ImGuiManager(entt::registry& registry);
    ImGuiManager(entt::registry& registry, Engine::Core* core);
    void ToggleEcsDebugger();

private:
    enum class WorldFileDialogMode {
        None,
        Open,
        Save
    };

    void EnsureDefaultWorldPath();
    void SetWorldPathBuffer(const std::filesystem::path& path);
    void DrawSceneMenuStatus();
    void DrawWorldFileDialog();
    void RequestWorldFileDialog(WorldFileDialogMode mode);
    void SaveCurrentWorldOrOpenDialog();
    bool SaveWorldToPath(const std::filesystem::path& worldPath);

    Engine::Core* _core = nullptr;
    std::array<char, 512> _worldPathBuffer {};
    WorldFileDialogMode _worldFileDialogMode = WorldFileDialogMode::None;
    bool _openWorldFileDialogRequested = false;
    bool _overwriteConfirmationActive = false;
    std::string _saveStatusText;
    float _saveStatusTimer = 0.0f;
    bool _lastSaveSucceeded = false;
};
