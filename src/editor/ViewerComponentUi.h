#pragma once

#include <entt/entt.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "Transform.h"
#include "NameComponent.h"

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
    void Draw(entt::registry& registry, entt::entity entity) override {
        auto* transform = registry.try_get<Transform>(entity);
         if (!transform)
            return;

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("Position", &transform->position.x, 0.1f);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(transform->rotation));
            if (ImGui::DragFloat3("Rotation", &euler.x, 0.1f))
            {
                transform->rotation = glm::normalize(glm::quat(glm::radians(euler)));
            }
            ImGui::DragFloat3("Scale", &transform->scale.x, 0.1f);
        }
    }
};