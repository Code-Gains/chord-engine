#include "GravitySimulationSystem.h"

#include "Core.h"
#include "GravityComponents.h"
#include "Transform.h"

#include <glm/geometric.hpp>
#include <unordered_map>

namespace {
    struct GravityAccumulation {
        glm::vec3 acceleration{ 0.0f };
        entt::entity dominantSource{ entt::null };
        float dominantAccelerationSq = 0.0f;
    };

    void AddAcceleration(
        std::unordered_map<entt::entity, GravityAccumulation>& accumulations,
        entt::entity target,
        entt::entity source,
        const glm::vec3& acceleration)
    {
        auto& accumulation = accumulations[target];
        accumulation.acceleration += acceleration;

        const float accelerationSq = glm::dot(acceleration, acceleration);
        if (accelerationSq > accumulation.dominantAccelerationSq) {
            accumulation.dominantAccelerationSq = accelerationSq;
            accumulation.dominantSource = source;
        }
    }
}

GravitySimulationSystem::GravitySimulationSystem(entt::registry& registry, Engine::Core* core)
    : System(registry)
    , _core(core)
{
}

void GravitySimulationSystem::FixedUpdate(float deltaTime)
{
    if (_core && !_core->IsPlayMode()) {
        return;
    }

    std::unordered_map<entt::entity, GravityAccumulation> accumulations;

    auto bodyView = _registry.view<Transform, GravityBodyComponent, VelocityComponent>();
    auto particleView = _registry.view<Transform, GravityParticleComponent, VelocityComponent>(
        entt::exclude<GravityBodyComponent>
    );

    for (auto entity : bodyView) {
        auto& gravityState = _registry.get_or_emplace<GravityStateComponent>(entity);
        gravityState.acceleration = glm::vec3{ 0.0f };
        gravityState.gravityDirection = glm::vec3{ 0.0f, -1.0f, 0.0f };
        gravityState.dominantSource = entt::null;
    }

    for (auto entity : particleView) {
        auto& gravityState = _registry.get_or_emplace<GravityStateComponent>(entity);
        gravityState.acceleration = glm::vec3{ 0.0f };
        gravityState.gravityDirection = glm::vec3{ 0.0f, -1.0f, 0.0f };
        gravityState.dominantSource = entt::null;
    }

    for (auto itA = bodyView.begin(); itA != bodyView.end(); ++itA) {
        auto entityA = *itA;
        auto& transformA = bodyView.get<Transform>(entityA);
        auto& bodyA = bodyView.get<GravityBodyComponent>(entityA);

        auto itB = itA;
        ++itB;

        for (; itB != bodyView.end(); ++itB) {
            auto entityB = *itB;
            auto& transformB = bodyView.get<Transform>(entityB);
            auto& bodyB = bodyView.get<GravityBodyComponent>(entityB);

            glm::vec3 distanceVector = transformB.position - transformA.position;
            float distanceSquared = glm::dot(distanceVector, distanceVector);
            if (distanceSquared < minDistanceSquared) {
                continue;
            }

            glm::vec3 direction = glm::normalize(distanceVector);
            AddAcceleration(
                accumulations,
                entityA,
                entityB,
                direction * gravitationalConstant * bodyB.mass / distanceSquared
            );
            AddAcceleration(
                accumulations,
                entityB,
                entityA,
                -direction * gravitationalConstant * bodyA.mass / distanceSquared
            );
        }
    }

    for (auto particleEntity : particleView) {
        auto& particleTransform = particleView.get<Transform>(particleEntity);
        auto& particle = particleView.get<GravityParticleComponent>(particleEntity);

        for (auto bodyEntity : bodyView) {
            auto& bodyTransform = bodyView.get<Transform>(bodyEntity);
            auto& body = bodyView.get<GravityBodyComponent>(bodyEntity);

            glm::vec3 distanceVector = bodyTransform.position - particleTransform.position;
            float distanceSquared = glm::dot(distanceVector, distanceVector);
            if (distanceSquared < minDistanceSquared) {
                continue;
            }

            glm::vec3 direction = glm::normalize(distanceVector);
            AddAcceleration(
                accumulations,
                particleEntity,
                bodyEntity,
                direction * gravitationalConstant * body.mass * particle.gravityScale / distanceSquared
            );
        }
    }

    for (auto& [entity, accumulation] : accumulations) {
        if (!_registry.valid(entity) || !_registry.all_of<VelocityComponent>(entity)) {
            continue;
        }

        auto& velocity = _registry.get<VelocityComponent>(entity);
        velocity.linear += accumulation.acceleration * deltaTime;

        auto& gravityState = _registry.get_or_emplace<GravityStateComponent>(entity);
        gravityState.acceleration = accumulation.acceleration;
        gravityState.dominantSource = accumulation.dominantSource;

        if (glm::dot(accumulation.acceleration, accumulation.acceleration) > 0.0f) {
            gravityState.gravityDirection = glm::normalize(accumulation.acceleration);
        }
    }
}
