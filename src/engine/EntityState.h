#pragma once

#include "HierarchyComponent.h"

#include <entt/entt.hpp>
#include <unordered_set>

struct DisabledEntityTag {};

inline bool IsEntityDisabled(entt::registry& registry, entt::entity entity)
{
    std::unordered_set<entt::entity> visited;

    while (entity != entt::null && registry.valid(entity)) {
        if (registry.all_of<DisabledEntityTag>(entity)) {
            return true;
        }

        if (!visited.insert(entity).second) {
            return false;
        }

        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy) {
            return false;
        }

        entity = hierarchy->parent;
    }

    return false;
}
