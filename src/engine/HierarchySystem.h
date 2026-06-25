#pragma once

#include "Transform.h"

#include <entt/entt.hpp>

namespace Engine {

void ResolveHierarchyTransforms(entt::registry& registry);
bool WouldCreateHierarchyCycle(
    entt::registry& registry,
    entt::entity child,
    entt::entity candidateParent);
void SetHierarchyParent(
    entt::registry& registry,
    entt::entity child,
    entt::entity parent,
    bool preserveWorldTransform = true);
void SetHierarchyParentLocal(
    entt::registry& registry,
    entt::entity child,
    entt::entity parent,
    const Transform& localTransform);
void SetHierarchyParentOrganizational(
    entt::registry& registry,
    entt::entity child,
    entt::entity parent);
Transform ComposeHierarchyWorldTransform(
    entt::registry& registry,
    entt::entity parent,
    const Transform& localTransform);
Transform ComputeHierarchyLocalTransform(
    entt::registry& registry,
    entt::entity parent,
    const Transform& worldTransform);

} // namespace Engine
