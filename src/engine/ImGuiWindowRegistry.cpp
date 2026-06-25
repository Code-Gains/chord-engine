#include "ImGuiWindowRegistry.h"

#include <imgui.h>

void ImGuiWindowRegistry::RegisterWindow(
    const std::string& menuName,
    bool defaultEnabled)
{
    for (const auto& window : _windows)
    {
        if (window.menuName == menuName)
            return;
    }

    _windows.push_back({
        menuName,
        defaultEnabled
    });
}

void ImGuiWindowRegistry::DrawMenuItems()
{
    for (auto& window : _windows)
    {
        const bool wasEnabled = window.enabled;
        ImGui::MenuItem(
            window.menuName.c_str(),
            nullptr,
            &window.enabled
        );

        if (!wasEnabled && window.enabled) {
            _windowsHidden = false;
            _hiddenWindowSnapshot.clear();
        }
    }
}

bool ImGuiWindowRegistry::IsWindowOpen(const std::string& menuName) const
{
    for (const auto& window : _windows)
    {
        if (window.menuName == menuName)
            return window.enabled;
    }

    return false;
}

void ImGuiWindowRegistry::SetWindowOpen(
    const std::string& menuName,
    bool open)
{
    for (auto& window : _windows)
    {
        if (window.menuName == menuName)
        {
            window.enabled = open;
            if (open) {
                _windowsHidden = false;
                _hiddenWindowSnapshot.clear();
            }
            return;
        }
    }
}

void ImGuiWindowRegistry::ToggleWindow(const std::string& menuName)
{
    for (auto& window : _windows)
    {
        if (window.menuName == menuName)
        {
            window.enabled = !window.enabled;
            if (window.enabled) {
                _windowsHidden = false;
                _hiddenWindowSnapshot.clear();
            }
            return;
        }
    }
}

void ImGuiWindowRegistry::HideAllWindows()
{
    _hiddenWindowSnapshot = _windows;
    bool anyOpen = false;

    for (auto& window : _windows) {
        anyOpen = anyOpen || window.enabled;
        window.enabled = false;
    }

    _windowsHidden = anyOpen;
}

void ImGuiWindowRegistry::RestoreHiddenWindows()
{
    if (!_windowsHidden) {
        return;
    }

    for (const auto& snapshotWindow : _hiddenWindowSnapshot) {
        for (auto& window : _windows) {
            if (window.menuName == snapshotWindow.menuName) {
                window.enabled = snapshotWindow.enabled;
                break;
            }
        }
    }

    _hiddenWindowSnapshot.clear();
    _windowsHidden = false;
}

void ImGuiWindowRegistry::ToggleHiddenWindows()
{
    if (_windowsHidden) {
        RestoreHiddenWindows();
        return;
    }

    HideAllWindows();
}

bool ImGuiWindowRegistry::HasHiddenWindows() const
{
    return _windowsHidden;
}

std::vector<std::string> ImGuiWindowRegistry::WindowNames() const
{
    std::vector<std::string> names;
    names.reserve(_windows.size());

    for (const auto& window : _windows) {
        names.push_back(window.menuName);
    }

    return names;
}
