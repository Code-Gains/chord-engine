#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "NameComponent.h"

void RegistryViewer::Update(float deltaTime)
{
}

void RegistryViewer::DrawUi()
{
      ImGui::Begin("Registry Viewer");

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

    ImGui::End();
}

RegistryViewer::RegistryViewer(entt::registry &registry) : System(registry) {
    //_componentUis.push_back(std::make_unique<TransformComponentUi>());
}

const entt::entity& RegistryViewer::GetSelectedEntity() const
{
    return _selectedEntity;
}
