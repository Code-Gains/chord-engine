#include "EntityViewer.h"
#include <ImGuiWindowRegistry.h>
#include "Camera.h"
#include "NameComponent.h"
#include "SunlightComponent.h"
#include "Transform.h"
#include "MeshComponent.h"

#include <string>

void EntityViewer::Update(float deltaTime)
{
}

void EntityViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (!windowRegistry.IsWindowOpen("Entity Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Entity Viewer", &open))
    {
        auto& selectedEntity = _registryViewerPtr->GetSelectedEntity();

        if (selectedEntity != entt::null && _registry.valid(selectedEntity))
        {
            if (ImGui::Button("+ Component")) {
                ImGui::OpenPopup("AddComponentPopup");
            }

            if (ImGui::BeginPopup("AddComponentPopup"))
            {
                bool hasAvailableComponent = false;

                if (!_registry.all_of<NameComponent>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Name")) {
                        _registry.emplace<NameComponent>(
                            selectedEntity,
                            "Entity " + std::to_string((int)entt::to_integral(selectedEntity))
                        );
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!_registry.all_of<Transform>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Transform")) {
                        _registry.emplace<Transform>(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!_registry.all_of<SunlightComponent>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Sunlight")) {
                        _registry.emplace<SunlightComponent>(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!_registry.all_of<Camera>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Camera")) {
                        _registry.emplace<Camera>(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!_registry.all_of<SingleRenderTag>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Single Render Tag")) {
                        _registry.emplace<SingleRenderTag>(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!_registry.all_of<MeshComponent>(selectedEntity)) {
                    hasAvailableComponent = true;
                    if (ImGui::MenuItem("Mesh")) {
                        _registry.emplace<MeshComponent>(selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                }

                if (!hasAvailableComponent) {
                    ImGui::MenuItem("All basic components added", nullptr, false, false);
                }

                ImGui::EndPopup();
            }

            ImGui::Separator();

            for (auto& ui : _componentUis)
            {
                ui->Draw(_registry, selectedEntity);
            }
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Entity Viewer", open);
}

EntityViewer::EntityViewer(entt::registry &registry, RegistryViewer* registryViewerPtr) : System(registry), _registryViewerPtr(registryViewerPtr)
{
    _componentUis.push_back(std::make_unique<NameComponentUi>());
    _componentUis.push_back(std::make_unique<TransformComponentUi>());
    _componentUis.push_back(std::make_unique<SunlightComponentUI>());
    _componentUis.push_back(std::make_unique<CameraComponentUi>());
    _componentUis.push_back(std::make_unique<MeshComponentUi>());
    _componentUis.push_back(std::make_unique<SingleRenderTagUi>());

    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Entity Viewer",
        true
    );
}
