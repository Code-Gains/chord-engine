#include "JoltColliderDebugDrawSystem.h"

#include "Core.h"
#include "EntityState.h"
#include "ImGuiWindowRegistry.h"
#include "JoltPhysicsComponents.h"
#include "LineComponent.h"
#include "Transform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

namespace {
    constexpr glm::vec4 StaticColliderColor{ 0.25f, 0.95f, 0.65f, 1.0f };
    constexpr glm::vec4 KinematicColliderColor{ 0.25f, 0.6f, 1.0f, 1.0f };
    constexpr glm::vec4 SensorColliderColor{ 1.0f, 0.75f, 0.2f, 1.0f };
    constexpr int CircleSegments = 32;

    glm::vec3 AbsScale(const glm::vec3& scale)
    {
        return {
            std::max(0.0001f, std::abs(scale.x)),
            std::max(0.0001f, std::abs(scale.y)),
            std::max(0.0001f, std::abs(scale.z))
        };
    }

    float MaxScaleAxis(const glm::vec3& scale)
    {
        return std::max({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 0.0001f });
    }

    void AddDebugLine(
        entt::registry& registry,
        const glm::vec3& start,
        const glm::vec3& end,
        const glm::vec4& color)
    {
        auto entity = registry.create();
        registry.emplace<LineComponent>(
            entity,
            start,
            end,
            color,
            0.0f,
            false);
        registry.emplace<Engine::CoreOwnedTag>(entity);
    }

    glm::vec3 ToWorldPoint(
        const Transform& transform,
        const glm::vec3& scaledCenter,
        const glm::vec3& scaledLocalPoint)
    {
        return transform.position + transform.rotation * (scaledCenter + scaledLocalPoint);
    }

    void DrawBoxCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const glm::vec3 extents = glm::max(collider.halfExtents * scale, glm::vec3{ 0.0001f });

        std::array<glm::vec3, 8> corners {
            glm::vec3{ -extents.x, -extents.y, -extents.z },
            glm::vec3{  extents.x, -extents.y, -extents.z },
            glm::vec3{  extents.x,  extents.y, -extents.z },
            glm::vec3{ -extents.x,  extents.y, -extents.z },
            glm::vec3{ -extents.x, -extents.y,  extents.z },
            glm::vec3{  extents.x, -extents.y,  extents.z },
            glm::vec3{  extents.x,  extents.y,  extents.z },
            glm::vec3{ -extents.x,  extents.y,  extents.z },
        };

        for (auto& corner : corners) {
            corner = ToWorldPoint(transform, center, corner);
        }

        constexpr std::array<std::array<int, 2>, 12> edges {{
            {{ 0, 1 }}, {{ 1, 2 }}, {{ 2, 3 }}, {{ 3, 0 }},
            {{ 4, 5 }}, {{ 5, 6 }}, {{ 6, 7 }}, {{ 7, 4 }},
            {{ 0, 4 }}, {{ 1, 5 }}, {{ 2, 6 }}, {{ 3, 7 }},
        }};

        for (const auto& edge : edges) {
            AddDebugLine(registry, corners[edge[0]], corners[edge[1]], color);
        }
    }

    void DrawCircle(
        entt::registry& registry,
        const Transform& transform,
        const glm::vec3& center,
        const glm::vec3& axisA,
        const glm::vec3& axisB,
        float radius,
        const glm::vec4& color)
    {
        glm::vec3 previous = ToWorldPoint(transform, center, axisA * radius);
        for (int segment = 1; segment <= CircleSegments; ++segment) {
            const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(CircleSegments);
            const glm::vec3 local = axisA * std::cos(angle) * radius + axisB * std::sin(angle) * radius;
            const glm::vec3 current = ToWorldPoint(transform, center, local);
            AddDebugLine(registry, previous, current, color);
            previous = current;
        }
    }

    void DrawArc(
        entt::registry& registry,
        const Transform& transform,
        const glm::vec3& center,
        const glm::vec3& axisA,
        const glm::vec3& axisB,
        float radius,
        float startAngle,
        float endAngle,
        const glm::vec4& color)
    {
        constexpr int ArcSegments = CircleSegments / 2;
        glm::vec3 previous = ToWorldPoint(
            transform,
            center,
            axisA * std::cos(startAngle) * radius +
                axisB * std::sin(startAngle) * radius);
        for (int segment = 1; segment <= ArcSegments; ++segment) {
            const float fraction =
                static_cast<float>(segment) / static_cast<float>(ArcSegments);
            const float angle = startAngle + (endAngle - startAngle) * fraction;
            const glm::vec3 local =
                axisA * std::cos(angle) * radius +
                axisB * std::sin(angle) * radius;
            const glm::vec3 current = ToWorldPoint(transform, center, local);
            AddDebugLine(registry, previous, current, color);
            previous = current;
        }
    }

    void DrawSphereCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const float radius = std::max(0.0001f, collider.radius * MaxScaleAxis(transform.scale));

        DrawCircle(registry, transform, center, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, radius, color);
        DrawCircle(registry, transform, center, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);
        DrawCircle(registry, transform, center, glm::vec3{ 0.0f, 1.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);
    }

    void DrawCapsuleCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const float radius = std::max(0.0001f, collider.radius * std::max(scale.x, scale.z));
        const float halfHeight = std::max(0.0f, collider.capsuleHalfHeight * scale.y);
        const glm::vec3 top{ 0.0f, halfHeight, 0.0f };
        const glm::vec3 bottom{ 0.0f, -halfHeight, 0.0f };

        DrawCircle(registry, transform, center + top, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);
        DrawCircle(registry, transform, center + bottom, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);

        DrawArc(registry, transform, center + top, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, radius, 0.0f, glm::pi<float>(), color);
        DrawArc(registry, transform, center + bottom, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, radius, glm::pi<float>(), glm::two_pi<float>(), color);
        DrawArc(registry, transform, center + top, glm::vec3{ 0.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, radius, 0.0f, glm::pi<float>(), color);
        DrawArc(registry, transform, center + bottom, glm::vec3{ 0.0f, 0.0f, 1.0f }, glm::vec3{ 0.0f, 1.0f, 0.0f }, radius, glm::pi<float>(), glm::two_pi<float>(), color);

        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ radius, 0.0f, 0.0f }), ToWorldPoint(transform, center, bottom + glm::vec3{ radius, 0.0f, 0.0f }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ -radius, 0.0f, 0.0f }), ToWorldPoint(transform, center, bottom + glm::vec3{ -radius, 0.0f, 0.0f }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ 0.0f, 0.0f, radius }), ToWorldPoint(transform, center, bottom + glm::vec3{ 0.0f, 0.0f, radius }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ 0.0f, 0.0f, -radius }), ToWorldPoint(transform, center, bottom + glm::vec3{ 0.0f, 0.0f, -radius }), color);
    }

    void DrawCylinderCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const float radius = std::max(0.0001f, collider.radius * std::max(scale.x, scale.z));
        const float halfHeight = std::max(0.0001f, collider.cylinderHalfHeight * scale.y);
        const glm::vec3 top{ 0.0f, halfHeight, 0.0f };
        const glm::vec3 bottom{ 0.0f, -halfHeight, 0.0f };

        DrawCircle(registry, transform, center + top, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);
        DrawCircle(registry, transform, center + bottom, glm::vec3{ 1.0f, 0.0f, 0.0f }, glm::vec3{ 0.0f, 0.0f, 1.0f }, radius, color);

        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ radius, 0.0f, 0.0f }), ToWorldPoint(transform, center, bottom + glm::vec3{ radius, 0.0f, 0.0f }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ -radius, 0.0f, 0.0f }), ToWorldPoint(transform, center, bottom + glm::vec3{ -radius, 0.0f, 0.0f }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ 0.0f, 0.0f, radius }), ToWorldPoint(transform, center, bottom + glm::vec3{ 0.0f, 0.0f, radius }), color);
        AddDebugLine(registry, ToWorldPoint(transform, center, top + glm::vec3{ 0.0f, 0.0f, -radius }), ToWorldPoint(transform, center, bottom + glm::vec3{ 0.0f, 0.0f, -radius }), color);
    }

    void DrawConeCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const float radius = std::max(0.0001f, collider.radius * std::max(scale.x, scale.z));
        const float halfHeight = std::max(0.0001f, collider.coneHalfHeight * scale.y);
        const glm::vec3 apex{ 0.0f, halfHeight, 0.0f };
        const glm::vec3 base{ 0.0f, -halfHeight, 0.0f };

        DrawCircle(
            registry,
            transform,
            center + base,
            glm::vec3{ 1.0f, 0.0f, 0.0f },
            glm::vec3{ 0.0f, 0.0f, 1.0f },
            radius,
            color);

        constexpr std::array<glm::vec3, 8> baseDirections {{
            glm::vec3{ 1.0f, 0.0f, 0.0f },
            glm::vec3{ 0.70710678f, 0.0f, 0.70710678f },
            glm::vec3{ 0.0f, 0.0f, 1.0f },
            glm::vec3{ -0.70710678f, 0.0f, 0.70710678f },
            glm::vec3{ -1.0f, 0.0f, 0.0f },
            glm::vec3{ -0.70710678f, 0.0f, -0.70710678f },
            glm::vec3{ 0.0f, 0.0f, -1.0f },
            glm::vec3{ 0.70710678f, 0.0f, -0.70710678f }
        }};
        const glm::vec3 worldApex = ToWorldPoint(transform, center, apex);
        for (const glm::vec3& direction : baseDirections) {
            AddDebugLine(
                registry,
                worldApex,
                ToWorldPoint(transform, center, base + direction * radius),
                color);
        }
    }

    void DrawConvexHullCollider(
        entt::registry& registry,
        const Transform& transform,
        const Engine::JoltColliderComponent& collider,
        const glm::vec4& color)
    {
        const glm::vec3 scale = AbsScale(transform.scale);
        const glm::vec3 center = collider.center * scale;
        const auto edges = Engine::BuildJoltConvexHullEdges(collider.convexHullPoints);
        for (const auto& edge : edges) {
            AddDebugLine(
                registry,
                ToWorldPoint(transform, center, edge.start * scale),
                ToWorldPoint(transform, center, edge.end * scale),
                color);
        }
    }

    glm::vec4 ResolveColliderColor(const Engine::JoltColliderComponent& collider)
    {
        if (collider.sensor) {
            return SensorColliderColor;
        }

        return collider.motion == Engine::JoltBodyMotion::Static
            ? StaticColliderColor
            : KinematicColliderColor;
    }
}

JoltColliderDebugDrawSystem::JoltColliderDebugDrawSystem(entt::registry& registry)
    : System(registry)
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    windowRegistry.RegisterWindow("Collider Debug", false);
}

void JoltColliderDebugDrawSystem::Update(float)
{
    if (!_showColliders) {
        return;
    }

    auto view = _registry.view<Transform, Engine::JoltColliderComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : view) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        const auto& transform = view.get<Transform>(entity);
        const auto& collider = view.get<Engine::JoltColliderComponent>(entity);
        if (_drawOnlyStatic && collider.motion != Engine::JoltBodyMotion::Static) {
            continue;
        }

        const glm::vec4 color = ResolveColliderColor(collider);
        switch (collider.shape) {
        case Engine::JoltColliderShape::Sphere:
            DrawSphereCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::Box:
            DrawBoxCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::Capsule:
            DrawCapsuleCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::Cylinder:
            DrawCylinderCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::Cone:
            DrawConeCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::ConvexHull:
            DrawConvexHullCollider(_registry, transform, collider, color);
            break;
        case Engine::JoltColliderShape::Compound:
            break;
        }
    }
}

void JoltColliderDebugDrawSystem::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();
    bool open = windowRegistry.IsWindowOpen("Collider Debug");
    if (!open) {
        return;
    }

    if (!ImGui::Begin("Collider Debug", &open)) {
        ImGui::End();
        windowRegistry.SetWindowOpen("Collider Debug", open);
        return;
    }

    ImGui::Checkbox("Show Colliders", &_showColliders);
    ImGui::Checkbox("Only Static", &_drawOnlyStatic);
    ImGui::TextDisabled("Static: green, kinematic: blue, sensor: yellow.");

    ImGui::End();
    windowRegistry.SetWindowOpen("Collider Debug", open);
}
