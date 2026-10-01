#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace Engine {

enum class JoltColliderShape {
    Sphere,
    Box,
    Capsule,
    Cylinder,
    Cone,
    ConvexHull,
    Compound
};

enum class JoltBodyMotion {
    Static,
    Kinematic
};

struct JoltColliderComponent {
    JoltColliderShape shape = JoltColliderShape::Sphere;
    JoltBodyMotion motion = JoltBodyMotion::Kinematic;
    bool sensor = false;
    glm::vec3 center{ 0.0f };
    float radius = 1.0f;
    glm::vec3 halfExtents{ 0.5f };
    float capsuleHalfHeight = 0.5f;
    float cylinderHalfHeight = 0.5f;
    float coneHalfHeight = 0.5f;
    std::vector<glm::vec3> convexHullPoints;
    float friction = 0.2f;
    float restitution = 0.0f;
};

struct JoltBodyComponent {
    uint32_t bodyId = 0xffffffffu;
    JoltColliderShape shape = JoltColliderShape::Sphere;
    JoltBodyMotion motion = JoltBodyMotion::Kinematic;
    bool sensor = false;
    glm::vec3 transformScale{ 1.0f };
    glm::vec3 center{ 0.0f };
    float radius = 1.0f;
    glm::vec3 halfExtents{ 0.5f };
    float capsuleHalfHeight = 0.5f;
    float cylinderHalfHeight = 0.5f;
    float coneHalfHeight = 0.5f;
    float friction = 0.2f;
    float restitution = 0.0f;
    uint64_t convexHullSignature = 0;
    uint64_t compoundSignature = 0;
};

struct JoltConvexHullEdge {
    glm::vec3 start{ 0.0f };
    glm::vec3 end{ 0.0f };
};

std::vector<glm::vec3> BuildJoltConvexHullPoints(std::span<const glm::vec3> sourcePoints);
std::vector<JoltConvexHullEdge> BuildJoltConvexHullEdges(std::span<const glm::vec3> hullPoints);

} // namespace Engine
