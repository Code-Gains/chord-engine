#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "NameComponent.h"
#include <ImGuiWindowRegistry.h>

void RegistryViewer::Update(float deltaTime)
{
}

void RegistryViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (!windowRegistry.IsWindowOpen("Registry Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Registry Viewer", &open))
    {
        auto view = _registry.view<Transform>();

        for (auto entity : view)
        {
            std::string label;

            if (auto* name = _registry.try_get<NameComponent>(entity))
            {
                label = name->name;
            }
            else
            {
                label = "Entity " + std::to_string((int)entt::to_integral(entity));
            }

            if (ImGui::Selectable(label.c_str(), _selectedEntity == entity))
            {
                _selectedEntity = entity;
            }
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Registry Viewer", open);
}

RegistryViewer::RegistryViewer(entt::registry &registry) : System(registry) {
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Registry Viewer",
        true
    );
}

const entt::entity& RegistryViewer::GetSelectedEntity() const
{
    return _selectedEntity;
}
