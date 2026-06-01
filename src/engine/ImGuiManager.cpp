#include "ImGuiManager.h"
#include "ImGuiWindowRegistry.h"

#include <imgui.h>

ImGuiManager::ImGuiManager(entt::registry& registry)
    : System(registry)
{
}

void ImGuiManager::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            ImGui::MenuItem("New", "Ctrl+N");
            ImGui::MenuItem("Open...", "Ctrl+O");
            ImGui::MenuItem("Save", "Ctrl+S");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            windowRegistry.DrawMenuItems();
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}