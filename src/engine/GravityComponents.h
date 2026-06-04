#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

struct VelocityComponent {
    glm::vec3 linear{ 0.0f };
    glm::vec3 angular{ 0.0f };
};

struct GravityBodyComponent {
    float mass = 1.0f;
};

struct GravityParticleComponent {
    float gravityScale = 1.0f;
};

struct GravityStateComponent {
    glm::vec3 acceleration{ 0.0f };
    glm::vec3 gravityDirection{ 0.0f, -1.0f, 0.0f };
    entt::entity dominantSource{ entt::null };
};
