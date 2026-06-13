#include "HierarchySystem.h"

#include "HierarchyComponent.h"
#include "Transform.h"

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <unordered_set>

namespace Engine {
namespace {

glm::mat4 ComposeMatrix(const Transform& transform)
{
    return transform.GetModelMatrix();
}

Transform TransformFromMatrix(const glm::mat4& matrix)
{
    Transform transform;
    glm::vec3 skew;
    glm::vec4 perspective;

    glm::decompose(
        matrix,
        transform.scale,
        transform.rotation,
        transform.position,
        skew,
        perspective
    );

    transform.rotation = glm::normalize(glm::conjugate(transform.rotation));
    return transform;
}

void ResolveEntity(
    entt::registry& registry,
    entt::entity entity,
    std::unordered_set<entt::entity>& resolving,
    std::unordered_set<entt::entity>& resolved)
{
    if (resolved.contains(entity) || resolving.contains(entity)) {
        return;
    }

    auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
    auto* transform = registry.try_get<Transform>(entity);
    if (!hierarchy || !transform) {
        return;
    }

    if (!hierarchy->inheritTransform ||
        hierarchy->parent == entt::null ||
        !registry.valid(hierarchy->parent) ||
        !registry.all_of<Transform>(hierarchy->parent)) {
        if (hierarchy->inheritTransform) {
            hierarchy->parent = entt::null;
        }
        resolved.insert(entity);
        return;
    }

    resolving.insert(entity);

    if (registry.all_of<HierarchyComponent>(hierarchy->parent)) {
        ResolveEntity(registry, hierarchy->parent, resolving, resolved);
    }

    const auto& parentTransform = registry.get<Transform>(hierarchy->parent);
    *transform = TransformFromMatrix(
        ComposeMatrix(parentTransform) * ComposeMatrix(hierarchy->localTransform)
    );

    resolving.erase(entity);
    resolved.insert(entity);
}

} // namespace

bool WouldCreateHierarchyCycle(
    entt::registry& registry,
    entt::entity child,
    entt::entity candidateParent)
{
    if (child == entt::null || candidateParent == entt::null) {
        return false;
    }

    if (child == candidateParent) {
        return true;
    }

    entt::entity current = candidateParent;
    while (current != entt::null && registry.valid(current)) {
        if (current == child) {
            return true;
        }

        auto* hierarchy = registry.try_get<HierarchyComponent>(current);
        if (!hierarchy) {
            return false;
        }

        current = hierarchy->parent;
    }

    return false;
}

void SetHierarchyParent(
    entt::registry& registry,
    entt::entity child,
    entt::entity parent,
    bool preserveWorldTransform)
{
    if (child == entt::null ||
        !registry.valid(child) ||
        !registry.all_of<Transform>(child) ||
        parent == child ||
        (parent != entt::null && !registry.valid(parent)) ||
        WouldCreateHierarchyCycle(registry, child, parent)) {
        return;
    }

    auto& hierarchy = registry.get_or_emplace<HierarchyComponent>(child);
    auto& childTransform = registry.get<Transform>(child);

    if (preserveWorldTransform && parent != entt::null && registry.all_of<Transform>(parent)) {
        const glm::mat4 localMatrix =
            glm::inverse(ComposeMatrix(registry.get<Transform>(parent))) *
            ComposeMatrix(childTransform);
        hierarchy.localTransform = TransformFromMatrix(localMatrix);
    }
    else if (preserveWorldTransform) {
        hierarchy.localTransform = childTransform;
    }

    hierarchy.parent = parent;
}

void ResolveHierarchyTransforms(entt::registry& registry)
{
    std::unordered_set<entt::entity> resolving;
    std::unordered_set<entt::entity> resolved;

    auto view = registry.view<HierarchyComponent, Transform>();
    for (auto entity : view) {
        ResolveEntity(registry, entity, resolving, resolved);
    }
}

} // namespace Engine
