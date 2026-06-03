#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Core.h"
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
        if (ImGui::Button("+ Entity"))
        {
            auto entity = _registry.create();
            _registry.emplace<Transform>(entity);
            _registry.emplace<NameComponent>(
                entity,
                "Entity " + std::to_string((int)entt::to_integral(entity))
            );

            _selectedEntity = entity;
        }

        ImGui::SameLine();

        const bool canDeleteSelectedEntity =
            _selectedEntity != entt::null &&
            _registry.valid(_selectedEntity) &&
            !_registry.all_of<Engine::CoreOwnedTag>(_selectedEntity);

        if (!canDeleteSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("- Entity"))
        {
            _registry.destroy(_selectedEntity);
            _selectedEntity = entt::null;
        }

        if (!canDeleteSelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::Separator();

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

            ImGui::PushID((int)entt::to_integral(entity));

            if (ImGui::Selectable(label.c_str(), _selectedEntity == entity))
            {
                _selectedEntity = entity;
            }

            ImGui::PopID();
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
