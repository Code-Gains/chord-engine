#pragma once

#include "Transform.h"

#include <entt/entt.hpp>

struct HierarchyComponent {
    entt::entity parent{ entt::null };
    bool inheritTransform = true;
    Transform localTransform{};
};
