#pragma once

#include "System.h"

#include <entt/entt.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace JPH {
class BodyInterface;
class JobSystemThreadPool;
class PhysicsSystem;
class TempAllocatorImpl;
}

namespace Engine {

class Core;

class JoltPhysicsSystem : public System {
public:
    struct ShapeHit {
        entt::entity entity{ entt::null };
        glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
        glm::vec3 contactPoint{ 0.0f };
        float penetration = 0.0f;
    };

    JoltPhysicsSystem(entt::registry& registry, Core* core);
    ~JoltPhysicsSystem() override;

    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override;
    void OnPlayStart() override;
    void OnPlayStop() override;
    std::vector<ShapeHit> CollideSphereWithStatic(
        const glm::vec3& position,
        float radius) const;

private:
    void InitializeJolt();
    void BuildCompoundAuthoringCache();
    void SyncBodies();
    void SyncRuntimeBodies();
    void RemoveStaleBodies();
    void RemoveBody(entt::entity entity);
    void CreateOrUpdateBody(entt::entity entity, uint64_t compoundSignature);
    bool BodyMatchesAuthoring(entt::entity entity, uint64_t compoundSignature) const;

    Core* _core = nullptr;
    std::unique_ptr<JPH::PhysicsSystem> _physicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> _tempAllocator;
    std::unique_ptr<JPH::JobSystemThreadPool> _jobSystem;
    std::unordered_map<entt::entity, uint32_t> _bodies;
    std::unordered_map<uint32_t, entt::entity> _bodyEntities;
    std::unordered_map<entt::entity, entt::entity> _compoundOwners;
    std::unordered_map<entt::entity, uint64_t> _compoundSignatures;
    std::size_t _cachedColliderEntityCount = 0;
    bool _compoundAuthoringCacheValid = false;
};

} // namespace Engine
