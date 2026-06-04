#pragma once

#include <entt/entt.hpp>
#include <cstdio>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Transform.h"
#include "Core.h"
#include "NameComponent.h"
#include "SunlightComponent.h"
#include "Camera.h"
#include "MeshComponent.h"
#include "GravityComponents.h"

class ViewerComponentUi
{
public:
    virtual ~ViewerComponentUi() = default;
    virtual void Draw(entt::registry& registry, entt::entity entity) = 0;

protected:
    bool DrawComponentHeader(const char* label, const char* id)
    {
        const std::string headerId = std::string(label) + "##" + id;
        return ImGui::CollapsingHeader(headerId.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    }

    template<typename Component>
    bool DrawRemovableComponentHeader(
        entt::registry& registry,
        entt::entity entity,
        const char* label,
        const char* id)
    {
        const std::string headerId = std::string(label) + "##" + id;
        bool componentVisible = true;
        const bool open = ImGui::CollapsingHeader(
            headerId.c_str(),
            &componentVisible,
            ImGuiTreeNodeFlags_DefaultOpen
        );

        if (!componentVisible) {
            registry.remove<Component>(entity);
            return false;
        }

        return open;
    }
};

class NameComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* nameComponent = registry.try_get<NameComponent>(entity);
        if (!nameComponent)
            return;

        if (DrawRemovableComponentHeader<NameComponent>(registry, entity, "Name", "NameComponent"))
        {
            char buffer[256] {};
            std::snprintf(
                buffer,
                sizeof(buffer),
                "%s",
                nameComponent->name.c_str()
            );

            if (ImGui::InputText("Name##NameComponentValue", buffer, sizeof(buffer))) {
                nameComponent->name = buffer;
            }
        }
    }
};

class TransformComponentUi : public ViewerComponentUi {
public:
    glm::vec3 rotationEulerDegrees{0.0f};

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* transform = registry.try_get<Transform>(entity);
        if (!transform)
            return;

        if (DrawComponentHeader("Transform", "TransformComponent"))
        {
            ImGui::TextUnformatted("Required");

            ImGui::DragFloat3("Position", &transform->position.x, 0.1f);

            if (ImGui::DragFloat3("Rotation", &rotationEulerDegrees.x, 0.1f))
            {
                glm::vec3 radians = glm::radians(rotationEulerDegrees);

                transform->rotation =
                    glm::normalize(glm::quat(radians));
            }

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
        }
    }
};

class SunlightComponentUI : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* sunlight = registry.try_get<SunlightComponent>(entity);
        if (!sunlight)
            return;

        if (DrawRemovableComponentHeader<SunlightComponent>(registry, entity, "Sunlight", "SunlightComponent"))
        {
            ImGui::DragFloat3("Direction", &sunlight->direction.x, 0.01f, -1.0f, 1.0f);

            if (glm::length(sunlight->direction) > 0.001f) {
                sunlight->direction = glm::normalize(sunlight->direction);
            }

            ImGui::DragFloat("Intensity", &sunlight->intensity, 0.05f, 0.0f, 20.0f);

            ImGui::ColorEdit3("Color", &sunlight->color.x);

            ImGui::DragFloat("Ambient", &sunlight->ambient, 0.01f, 0.0f, 1.0f);
        }
    }
};

class CameraComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* camera = registry.try_get<Camera>(entity);
        if (!camera)
            return;

        if (DrawRemovableComponentHeader<Camera>(registry, entity, "Camera", "CameraComponent"))
        {
            const bool isEditorOwned = registry.all_of<Engine::CoreOwnedTag>(entity);
            if (!isEditorOwned) {
                if (registry.all_of<EditorCameraPilotTag>(entity)) {
                    if (ImGui::Button("Stop Piloting##CameraPilot")) {
                        registry.remove<EditorCameraPilotTag>(entity);
                    }
                }
                else {
                    if (ImGui::Button("Pilot Camera##CameraPilot")) {
                        auto pilotView = registry.view<EditorCameraPilotTag>();
                        for (auto pilotEntity : pilotView) {
                            registry.remove<EditorCameraPilotTag>(pilotEntity);
                        }

                        registry.emplace<EditorCameraPilotTag>(entity);
                    }
                }
            }

            ImGui::DragFloat("FOV", &camera->fov, 0.5f, 1.0f, 179.0f);
            ImGui::DragFloat("Near Plane", &camera->nearPlane, 0.01f, 0.001f, 1000.0f);
            ImGui::DragFloat("Far Plane", &camera->farPlane, 10.0f, 1.0f, 1000000.0f);
            ImGui::DragFloat("Speed", &camera->speed, 0.5f, 0.0f, 10000.0f);
            ImGui::ColorEdit4("Clear Color", &camera->clearColor.x);
        }
    }
};

class MeshComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* mesh = registry.try_get<MeshComponent>(entity);
        if (!mesh)
            return;

        if (DrawRemovableComponentHeader<MeshComponent>(registry, entity, "Mesh", "MeshComponent"))
        {
            MeshAssetReference source = mesh->source;
            if (!source.IsValid() && mesh->mesh) {
                source = mesh->mesh->source;
            }

            ImGui::Text("Path: %s", source.path.c_str());
            ImGui::Text("Mesh Index: %u", source.meshIndex);
        }
    }
};

class SingleRenderTagUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        if (!registry.all_of<SingleRenderTag>(entity))
            return;

        if (DrawRemovableComponentHeader<SingleRenderTag>(registry, entity, "Single Render", "SingleRenderTag"))
        {
            ImGui::TextUnformatted("Enabled");
        }
    }
};

class ActiveCameraTagUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        if (!registry.all_of<ActiveCameraTag>(entity))
            return;

        if (DrawRemovableComponentHeader<ActiveCameraTag>(registry, entity, "Active Camera", "ActiveCameraTag"))
        {
            ImGui::TextUnformatted("Used for rendering in Play Mode");
        }
    }
};

class VelocityComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* velocity = registry.try_get<VelocityComponent>(entity);
        if (!velocity)
            return;

        if (DrawRemovableComponentHeader<VelocityComponent>(registry, entity, "Velocity", "VelocityComponent"))
        {
            ImGui::DragFloat3("Linear", &velocity->linear.x, 0.1f);
            ImGui::DragFloat3("Angular", &velocity->angular.x, 0.1f);
        }
    }
};

class GravityBodyComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* gravityBody = registry.try_get<GravityBodyComponent>(entity);
        if (!gravityBody)
            return;

        if (DrawRemovableComponentHeader<GravityBodyComponent>(registry, entity, "Gravity Body", "GravityBodyComponent"))
        {
            ImGui::DragFloat("Mass", &gravityBody->mass, 0.1f, 0.0f, 1000000000.0f);
        }
    }
};

class GravityParticleComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* gravityParticle = registry.try_get<GravityParticleComponent>(entity);
        if (!gravityParticle)
            return;

        if (DrawRemovableComponentHeader<GravityParticleComponent>(registry, entity, "Gravity Particle", "GravityParticleComponent"))
        {
            ImGui::DragFloat("Gravity Scale", &gravityParticle->gravityScale, 0.01f, 0.0f, 1000.0f);
        }
    }
};
