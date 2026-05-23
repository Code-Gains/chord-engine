#pragma once

#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Transform.h"
#include "NameComponent.h"
#include "SunlightComponent.h"

class ViewerComponentUi
{
public:
    virtual ~ViewerComponentUi() = default;
    virtual void Draw(entt::registry& registry, entt::entity entity) = 0;
};

class NameComponentUi : public ViewerComponentUi {
public:
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* nameComponent = registry.try_get<NameComponent>(entity);
        if (!nameComponent)
            return;

        ImGui::Text("%s", nameComponent->name.c_str());
    }
};

class TransformComponentUi : public ViewerComponentUi {
public:
    glm::vec3 rotationEulerDegrees{0.0f};

    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* transform = registry.try_get<Transform>(entity);
        if (!transform)
            return;

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
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

        if (ImGui::CollapsingHeader("Sunlight", ImGuiTreeNodeFlags_DefaultOpen))
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