#include "EditorUiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>



void EditorUiManager::Update(float deltaTime) {
}

void EditorUiManager::DrawUi() {
     // Optional styling (keep or remove as needed)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    if (ImGui::BeginMainMenuBar())
    {
        // ---- File Menu ----
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("New", "Ctrl+N"))
            {
                // TODO: Handle New
            }

            if (ImGui::MenuItem("Open...", "Ctrl+O"))
            {
                // TODO: Handle Open
            }

            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                // TODO: Handle Save
            }

            if (ImGui::MenuItem("Save As..."))
            {
                // TODO: Handle Save As
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit"))
            {
                // TODO: Handle Exit
            }

            ImGui::EndMenu();
        }

        // ---- Window Menu ----
        if (ImGui::BeginMenu("Window"))
        {
            static bool showDemoWindow = false;
            static bool showAnotherWindow = false;

            ImGui::MenuItem("Demo Window", nullptr, &showDemoWindow);
            ImGui::MenuItem("Another Window", nullptr, &showAnotherWindow);

            ImGui::Separator();

            if (ImGui::MenuItem("Reset Layout"))
            {
                // TODO: Reset layout
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);

    // Optional: example windows
    static bool showDemoWindow = false;
    static bool showAnotherWindow = false;

    if (showDemoWindow)
        ImGui::ShowDemoWindow(&showDemoWindow);

    if (showAnotherWindow)
    {
        ImGui::Begin("Another Window", &showAnotherWindow);
        ImGui::Text("Placeholder content");
        ImGui::End();
    }
}

EditorUiManager::EditorUiManager(entt::registry &registry) : System(registry)
{
}

void EditorUiManager::ToggleEcsDebugger()
{
}
