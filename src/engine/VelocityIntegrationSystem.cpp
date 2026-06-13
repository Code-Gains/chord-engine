#include "VelocityIntegrationSystem.h"

#include "Core.h"
#include "GravityComponents.h"
#include "RuntimePauseState.h"
#include "Transform.h"

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

VelocityIntegrationSystem::VelocityIntegrationSystem(entt::registry& registry, Engine::Core* core)
    : System(registry)
    , _core(core)
{
}

void VelocityIntegrationSystem::FixedUpdate(float deltaTime)
{
    if (_core && !_core->IsPlayMode()) {
        return;
    }
    if (Engine::IsGameplayPaused(_registry)) {
        return;
    }

    auto view = _registry.view<Transform, VelocityComponent>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& velocity = view.get<VelocityComponent>(entity);

        transform.position += velocity.linear * deltaTime;

        const float angularSpeed = glm::length(velocity.angular);
        if (angularSpeed > 0.0f) {
            const glm::vec3 axis = velocity.angular / angularSpeed;
            transform.rotation = glm::normalize(
                glm::angleAxis(angularSpeed * deltaTime, axis) * transform.rotation
            );
        }
    }
}
