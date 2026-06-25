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
#include "HierarchyComponent.h"
#include "HierarchySystem.h"

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
            if (registry.all_of<HierarchyComponent>(entity)) {
                ImGui::TextDisabled("World transform. Edit local transform in Node.");
            }

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

class HierarchyComponentUi : public ViewerComponentUi {
public:
    glm::vec3 localRotationEulerDegrees{0.0f};
    entt::entity editedEntity{ entt::null };

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy)
            return;

        if (DrawRemovableComponentHeader<HierarchyComponent>(registry, entity, "Hierarchy", "HierarchyComponent"))
        {
            const int parentId = hierarchy->parent == entt::null
                ? -1
                : static_cast<int>(entt::to_integral(hierarchy->parent));

            ImGui::Text("Parent: %d", parentId);
            ImGui::TextDisabled(
                hierarchy->inheritTransform
                    ? "Mode: Attached"
                    : "Mode: Organizational");

            if (editedEntity != entity) {
                editedEntity = entity;
            }

            if (hierarchy->parent != entt::null && registry.valid(hierarchy->parent)) {
                if (hierarchy->inheritTransform) {
                    if (ImGui::Button("Make Organizational##HierarchyParent")) {
                        Engine::SetHierarchyParentOrganizational(registry, entity, hierarchy->parent);
                    }
                }
                else {
                    if (ImGui::Button("Make Attached##HierarchyParent")) {
                        Engine::SetHierarchyParent(registry, entity, hierarchy->parent, true);
                    }
                }
            }

            ImGui::SeparatorText("Local Transform");
            ImGui::TextDisabled("Edited relative to the parent when attached.");

            ImGui::DragFloat3("Local Position", &hierarchy->localTransform.position.x, 0.1f);

            if (ImGui::DragFloat3("Local Rotation", &localRotationEulerDegrees.x, 0.1f))
            {
                glm::vec3 radians = glm::radians(localRotationEulerDegrees);
                hierarchy->localTransform.rotation =
                    glm::normalize(glm::quat(radians));
            }

            ImGui::DragFloat3("Local Scale", &hierarchy->localTransform.scale.x, 0.1f);
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
            ImGui::ColorEdit4("Base Color Factor", &mesh->baseColorFactor.x);
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
