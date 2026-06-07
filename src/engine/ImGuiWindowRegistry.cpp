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
        ImGui::MenuItem(
            window.menuName.c_str(),
            nullptr,
            &window.enabled
        );
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
            return;
        }
    }
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
