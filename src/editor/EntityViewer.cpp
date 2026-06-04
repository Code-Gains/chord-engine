#include "EntityViewer.h"
#include <ImGuiWindowRegistry.h>
#include "Camera.h"
#include "Core.h"
#include "NameComponent.h"
#include "SunlightComponent.h"
#include "Transform.h"
#include "MeshComponent.h"
#include "GravityComponents.h"

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

                for (const auto& entry : _componentMenuEntries) {
                    if (!entry.canAdd(_registry, selectedEntity)) {
                        continue;
                    }

                    hasAvailableComponent = true;
                    if (ImGui::MenuItem(entry.label.c_str())) {
                        entry.add(_registry, selectedEntity);
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
    _componentUis.push_back(std::make_unique<ActiveCameraTagUi>());
    _componentUis.push_back(std::make_unique<VelocityComponentUi>());
    _componentUis.push_back(std::make_unique<GravityBodyComponentUi>());
    _componentUis.push_back(std::make_unique<GravityParticleComponentUi>());

    AddComponentMenuItem(
        "Name",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<NameComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<NameComponent>(
                entity,
                "Entity " + std::to_string((int)entt::to_integral(entity))
            );
        }
    );

    AddComponentMenuItem(
        "Transform",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<Transform>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<Transform>(entity);
        }
    );

    AddComponentMenuItem(
        "Sunlight",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<SunlightComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<SunlightComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Camera",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<Camera>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<Camera>(entity);

            auto activeCameraView =
                registry.view<ActiveCameraTag>(entt::exclude<Engine::CoreOwnedTag>);

            if (activeCameraView.begin() == activeCameraView.end()) {
                registry.emplace<ActiveCameraTag>(entity);
            }
        }
    );

    AddComponentMenuItem(
        "Single Render Tag",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<SingleRenderTag>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<SingleRenderTag>(entity);
        }
    );

    AddComponentMenuItem(
        "Active Camera",
        [](entt::registry& registry, entt::entity entity) {
            return registry.all_of<Camera>(entity) &&
                   !registry.all_of<ActiveCameraTag>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            auto activeCameraView =
                registry.view<ActiveCameraTag>(entt::exclude<Engine::CoreOwnedTag>);

            for (auto activeCameraEntity : activeCameraView) {
                registry.remove<ActiveCameraTag>(activeCameraEntity);
            }

            registry.emplace<ActiveCameraTag>(entity);
        }
    );

    AddComponentMenuItem(
        "Mesh",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<MeshComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<MeshComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Velocity",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<VelocityComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<VelocityComponent>(entity);
        }
    );

    AddComponentMenuItem(
        "Gravity Body",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<GravityBodyComponent>(entity) &&
                   !registry.all_of<GravityParticleComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<GravityBodyComponent>(entity);
            if (!registry.all_of<VelocityComponent>(entity)) {
                registry.emplace<VelocityComponent>(entity);
            }
        }
    );

    AddComponentMenuItem(
        "Gravity Particle",
        [](entt::registry& registry, entt::entity entity) {
            return !registry.all_of<GravityParticleComponent>(entity) &&
                   !registry.all_of<GravityBodyComponent>(entity);
        },
        [](entt::registry& registry, entt::entity entity) {
            registry.emplace<GravityParticleComponent>(entity);
            if (!registry.all_of<VelocityComponent>(entity)) {
                registry.emplace<VelocityComponent>(entity);
            }
        }
    );

    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Entity Viewer",
        true
    );
}

void EntityViewer::AddComponentUi(std::unique_ptr<ViewerComponentUi> componentUi)
{
    _componentUis.push_back(std::move(componentUi));
}

void EntityViewer::AddComponentMenuItem(
    std::string label,
    std::function<bool(entt::registry&, entt::entity)> canAdd,
    std::function<void(entt::registry&, entt::entity)> add)
{
    _componentMenuEntries.push_back(ComponentMenuEntry {
        std::move(label),
        std::move(canAdd),
        std::move(add)
    });
}
