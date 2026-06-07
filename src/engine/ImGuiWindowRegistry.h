#pragma once

#include <string>
#include <vector>

struct ImGuiWindowEntry
{
    std::string menuName;
    bool enabled = false;
};

class ImGuiWindowRegistry
{
public:
    void RegisterWindow(
        const std::string& menuName,
        bool defaultEnabled);

    void DrawMenuItems();

    bool IsWindowOpen(const std::string& menuName) const;
    void SetWindowOpen(const std::string& menuName, bool open);
    void ToggleWindow(const std::string& menuName);
    std::vector<std::string> WindowNames() const;

private:
    std::vector<ImGuiWindowEntry> _windows;
};
