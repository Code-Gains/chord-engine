#pragma once

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

} // namespace Engine

