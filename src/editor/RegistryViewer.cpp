#include "RegistryViewer.h"
#include <string>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include "Core.h"
#include "EditorSelection.h"
#include "InputSystem.h"
#include "NameComponent.h"
#include <ImGuiWindowRegistry.h>

namespace {
    EditorSelection& GetEditorSelection(entt::registry& registry)
    {
        if (!registry.ctx().contains<EditorSelection>()) {
            registry.ctx().emplace<EditorSelection>();
        }

        return registry.ctx().get<EditorSelection>();
    }

    void ClearSelectedEntity(entt::registry& registry, entt::entity& selectedEntity)
    {
        selectedEntity = entt::null;
        GetEditorSelection(registry).selectedEntity = entt::null;
    }

    bool CanDeleteSelectedEntity(entt::registry& registry, entt::entity selectedEntity)
    {
        return selectedEntity != entt::null &&
            registry.valid(selectedEntity) &&
            !registry.all_of<Engine::CoreOwnedTag>(selectedEntity);
    }

    void DeleteSelectedEntity(entt::registry& registry, entt::entity& selectedEntity)
    {
        if (!CanDeleteSelectedEntity(registry, selectedEntity)) {
            return;
        }

        registry.destroy(selectedEntity);
        ClearSelectedEntity(registry, selectedEntity);
    }
}

void RegistryViewer::Update(float deltaTime)
{
    auto inputView = _registry.view<InputState>();
    if (inputView.empty()) {
        return;
    }

    auto inputEntity = *inputView.begin();
    auto& input = inputView.get<InputState>(inputEntity);

    if (input.keys[GLFW_KEY_ESCAPE].pressed) {
        ClearSelectedEntity(_registry, _selectedEntity);
    }
}

void RegistryViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    auto& editorSelection = GetEditorSelection(_registry);

    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
        !ImGui::GetIO().WantTextInput &&
        !ImGui::IsAnyItemActive())
    {
        DeleteSelectedEntity(_registry, _selectedEntity);
    }

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
            editorSelection.selectedEntity = _selectedEntity;
        }

        ImGui::SameLine();

        const bool canDeleteSelectedEntity = CanDeleteSelectedEntity(_registry, _selectedEntity);

        if (!canDeleteSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("- Entity"))
        {
            DeleteSelectedEntity(_registry, _selectedEntity);
        }

        if (!canDeleteSelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();

        const bool hasSelectedEntity =
            _selectedEntity != entt::null &&
            _registry.valid(_selectedEntity);

        if (!hasSelectedEntity) {
            ImGui::BeginDisabled();
        }

        if (ImGui::Button("Clear"))
        {
            ClearSelectedEntity(_registry, _selectedEntity);
        }

        if (!hasSelectedEntity) {
            ImGui::EndDisabled();
        }

        ImGui::Checkbox("Show editor entities", &_showCoreOwnedEntities);

        ImGui::Separator();

        auto view = _registry.view<Transform>();

        for (auto entity : view)
        {
            if (!_showCoreOwnedEntities &&
                _registry.all_of<Engine::CoreOwnedTag>(entity))
            {
                if (_selectedEntity == entity) {
                    ClearSelectedEntity(_registry, _selectedEntity);
                }

                continue;
            }

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
                editorSelection.selectedEntity = _selectedEntity;
            }

            ImGui::PopID();
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Registry Viewer", open);
}

RegistryViewer::RegistryViewer(entt::registry &registry) : System(registry) {
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    GetEditorSelection(_registry);

    windowRegistry.RegisterWindow(
        "Registry Viewer",
        true
    );
}

const entt::entity& RegistryViewer::GetSelectedEntity() const
{
    return _selectedEntity;
}

void RegistryViewer::SetSelectedEntity(entt::entity entity)
{
    if (entity != entt::null && !_registry.valid(entity)) {
        ClearSelectedEntity(_registry, _selectedEntity);
        return;
    }

    _selectedEntity = entity;
    GetEditorSelection(_registry).selectedEntity = entity;
}
