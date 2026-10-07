#include "JoltPhysicsSystem.h"

#include "Core.h"
#include "EntityState.h"
#include "HierarchyComponent.h"
#include "JoltPhysicsComponents.h"
#include "Log.h"
#include "Transform.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <thread>
#include <unordered_set>

namespace Engine {
namespace {

namespace Layers {
    constexpr JPH::ObjectLayer Static = 0;
    constexpr JPH::ObjectLayer Kinematic = 1;
    constexpr JPH::ObjectLayer Count = 2;
}

namespace BroadPhaseLayers {
    constexpr JPH::BroadPhaseLayer Static(0);
    constexpr JPH::BroadPhaseLayer Kinematic(1);
    constexpr JPH::uint Count = 2;
}

class BroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::Count;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::Static
            ? BroadPhaseLayers::Static
            : BroadPhaseLayers::Kinematic;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return static_cast<JPH::BroadPhaseLayer::Type>(layer) == static_cast<JPH::BroadPhaseLayer::Type>(BroadPhaseLayers::Static)
            ? "Static"
            : "Kinematic";
    }
#endif
};

class ObjectVsBroadPhaseLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
    {
        if (layer == Layers::Static) {
            return broadPhaseLayer == BroadPhaseLayers::Kinematic;
        }

        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
    {
        return first != Layers::Static || second != Layers::Static;
    }
};

class StaticObjectLayerFilter final : public JPH::ObjectLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return layer == Layers::Static;
    }
};

BroadPhaseLayerInterface& GetBroadPhaseLayerInterface()
{
    static BroadPhaseLayerInterface interface;
    return interface;
}

ObjectVsBroadPhaseLayerFilter& GetObjectVsBroadPhaseLayerFilter()
{
    static ObjectVsBroadPhaseLayerFilter filter;
    return filter;
}

ObjectLayerPairFilter& GetObjectLayerPairFilter()
{
    static ObjectLayerPairFilter filter;
    return filter;
}

StaticObjectLayerFilter& GetStaticObjectLayerFilter()
{
    static StaticObjectLayerFilter filter;
    return filter;
}

void EnsureJoltTypesRegistered()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }

    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
    initialized = true;
}

JPH::Vec3 ToJoltVec3(const glm::vec3& value)
{
    return JPH::Vec3{ value.x, value.y, value.z };
}

JPH::Quat ToJoltQuat(const glm::quat& value)
{
    return JPH::Quat{ value.x, value.y, value.z, value.w };
}

glm::vec3 FromJoltVec3(const JPH::Vec3& value)
{
    return { value.GetX(), value.GetY(), value.GetZ() };
}

float MaxScaleAxis(const glm::vec3& scale)
{
    return std::max({ std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 0.0001f });
}

glm::vec3 AbsScale(const glm::vec3& scale)
{
    return {
        std::max(0.0001f, std::abs(scale.x)),
        std::max(0.0001f, std::abs(scale.y)),
        std::max(0.0001f, std::abs(scale.z))
    };
}

JPH::RefConst<JPH::Shape> CreateConvexHullShape(
    std::span<const glm::vec3> sourcePoints,
    const glm::vec3& scale,
    bool adaptTolerance)
{
    if (sourcePoints.size() < 4) {
        return nullptr;
    }

    JPH::Array<JPH::Vec3> points;
    points.reserve(sourcePoints.size());
    glm::vec3 minimum{ std::numeric_limits<float>::max() };
    glm::vec3 maximum{ std::numeric_limits<float>::lowest() };
    for (const glm::vec3& sourcePoint : sourcePoints) {
        const glm::vec3 point = sourcePoint * scale;
        minimum = glm::min(minimum, point);
        maximum = glm::max(maximum, point);
        points.push_back(ToJoltVec3(point));
    }

    const float extent = glm::max(glm::length(maximum - minimum), 0.001f);
    const int attemptCount = adaptTolerance ? 9 : 1;
    for (int attempt = 0; attempt < attemptCount; ++attempt) {
        JPH::ConvexHullShapeSettings settings(points, 0.0f);
        settings.mHullTolerance = extent * 0.0001f * std::pow(2.0f, static_cast<float>(attempt));
        auto result = settings.Create();
        if (!result.HasError()) {
            return result.Get();
        }
    }

    return nullptr;
}

JPH::RefConst<JPH::Shape> CreatePrimitiveShape(const JoltColliderComponent& collider, const Transform& transform)
{
    const glm::vec3 scale = AbsScale(transform.scale);

    JPH::RefConst<JPH::Shape> shape;
    switch (collider.shape) {
    case JoltColliderShape::Sphere:
        shape = new JPH::SphereShape(std::max(0.0001f, collider.radius * MaxScaleAxis(transform.scale)));
        break;
    case JoltColliderShape::Box:
        shape = new JPH::BoxShape(ToJoltVec3(glm::max(collider.halfExtents * scale, glm::vec3{ 0.0001f })));
        break;
    case JoltColliderShape::Capsule:
        shape = new JPH::CapsuleShape(
            std::max(0.0f, collider.capsuleHalfHeight * scale.y),
            std::max(0.0001f, collider.radius * std::max(scale.x, scale.z)));
        break;
    case JoltColliderShape::Cylinder: {
        const float halfHeight = std::max(0.0001f, collider.cylinderHalfHeight * scale.y);
        const float radius = std::max(0.0001f, collider.radius * std::max(scale.x, scale.z));
        const float convexRadius = std::min(0.05f, 0.5f * std::min(halfHeight, radius));
        shape = new JPH::CylinderShape(halfHeight, radius, convexRadius);
        break;
    }
    case JoltColliderShape::Cone: {
        constexpr int ConeSegments = 24;
        constexpr float TwoPi = 6.28318530718f;
        const float halfHeight = std::max(0.0001f, collider.coneHalfHeight * scale.y);
        const float radius = std::max(0.0001f, collider.radius * std::max(scale.x, scale.z));
        JPH::Array<JPH::Vec3> points;
        points.reserve(ConeSegments + 1);
        points.push_back(JPH::Vec3{ 0.0f, halfHeight, 0.0f });
        for (int segment = 0; segment < ConeSegments; ++segment) {
            const float angle = TwoPi * static_cast<float>(segment) /
                static_cast<float>(ConeSegments);
            points.push_back(JPH::Vec3{
                std::cos(angle) * radius,
                -halfHeight,
                std::sin(angle) * radius
            });
        }

        auto result = JPH::ConvexHullShapeSettings(points, 0.0f).Create();
        if (result.HasError()) {
            ENGINE_LOG_ERROR("Failed to create Jolt cone shape: " + result.GetError());
            return nullptr;
        }
        shape = result.Get();
        break;
    }
    case JoltColliderShape::ConvexHull:
        shape = CreateConvexHullShape(collider.convexHullPoints, scale, false);
        if (shape == nullptr) {
            ENGINE_LOG_ERROR("Failed to create Jolt convex hull collider.");
            return nullptr;
        }
        break;
    case JoltColliderShape::Compound:
        return nullptr;
    }

    if (glm::dot(collider.center, collider.center) <= 0.0000001f) {
        return shape;
    }

    return new JPH::RotatedTranslatedShape(
        ToJoltVec3(collider.center * scale),
        JPH::Quat::sIdentity(),
        shape);
}

entt::entity FindCompoundAncestor(entt::registry& registry, entt::entity entity)
{
    const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
    entt::entity current = hierarchy ? hierarchy->parent : entt::null;
    while (current != entt::null && registry.valid(current)) {
        if (const auto* collider = registry.try_get<JoltColliderComponent>(current);
            collider && collider->shape == JoltColliderShape::Compound) {
            return current;
        }

        const auto* parentHierarchy = registry.try_get<HierarchyComponent>(current);
        current = parentHierarchy ? parentHierarchy->parent : entt::null;
    }
    return entt::null;
}

entt::entity FindOutermostCompoundAncestor(entt::registry& registry, entt::entity entity)
{
    entt::entity compoundAncestor = entt::null;
    const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
    entt::entity current = hierarchy ? hierarchy->parent : entt::null;
    while (current != entt::null && registry.valid(current)) {
        if (const auto* collider = registry.try_get<JoltColliderComponent>(current);
            collider && collider->shape == JoltColliderShape::Compound) {
            compoundAncestor = current;
        }

        const auto* parentHierarchy = registry.try_get<HierarchyComponent>(current);
        current = parentHierarchy ? parentHierarchy->parent : entt::null;
    }
    return compoundAncestor;
}

bool IsDescendantOf(entt::registry& registry, entt::entity entity, entt::entity ancestor)
{
    const auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
    entt::entity current = hierarchy ? hierarchy->parent : entt::null;
    while (current != entt::null && registry.valid(current)) {
        if (current == ancestor) {
            return true;
        }
        const auto* parentHierarchy = registry.try_get<HierarchyComponent>(current);
        current = parentHierarchy ? parentHierarchy->parent : entt::null;
    }
    return false;
}

void HashCombine(uint64_t& seed, uint64_t value)
{
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

void HashFloat(uint64_t& seed, float value)
{
    HashCombine(seed, std::bit_cast<uint32_t>(value));
}

uint64_t ConvexHullSignature(const JoltColliderComponent& collider)
{
    uint64_t signature = collider.convexHullPoints.size();
    for (const glm::vec3& point : collider.convexHullPoints) {
        HashFloat(signature, point.x);
        HashFloat(signature, point.y);
        HashFloat(signature, point.z);
    }
    return signature;
}

void AppendCompoundChildSignature(
    entt::registry& registry,
    entt::entity root,
    entt::entity entity,
    uint64_t& signature)
{
    const auto& rootTransform = registry.get<Transform>(root);
    const glm::quat inverseRootRotation = glm::inverse(glm::normalize(rootTransform.rotation));
    const auto& collider = registry.get<JoltColliderComponent>(entity);
    const auto& transform = registry.get<Transform>(entity);
    const glm::vec3 localPosition =
        inverseRootRotation * (transform.position - rootTransform.position);
    const glm::quat localRotation = glm::normalize(
        inverseRootRotation * transform.rotation);
    HashCombine(signature, static_cast<uint64_t>(entt::to_integral(entity)));
    HashCombine(signature, static_cast<uint64_t>(collider.shape));
    for (const float value : {
             localPosition.x, localPosition.y, localPosition.z,
             localRotation.w, localRotation.x, localRotation.y, localRotation.z,
             transform.scale.x, transform.scale.y, transform.scale.z,
             collider.center.x, collider.center.y, collider.center.z,
             collider.radius,
             collider.halfExtents.x, collider.halfExtents.y, collider.halfExtents.z,
             collider.capsuleHalfHeight,
             collider.cylinderHalfHeight,
             collider.coneHalfHeight }) {
        HashFloat(signature, value);
    }
    if (collider.shape == JoltColliderShape::ConvexHull) {
        HashCombine(signature, ConvexHullSignature(collider));
    }
}

JPH::RefConst<JPH::Shape> CreateCompoundShape(
    entt::registry& registry,
    entt::entity root,
    const JoltColliderComponent& rootCollider,
    const Transform& rootTransform)
{
    JPH::StaticCompoundShapeSettings settings;
    const glm::quat inverseRootRotation = glm::inverse(glm::normalize(rootTransform.rotation));
    const glm::vec3 rootOffset = rootCollider.center * AbsScale(rootTransform.scale);

    auto view = registry.view<Transform, JoltColliderComponent>(entt::exclude<DisabledEntityTag>);
    for (const entt::entity entity : view) {
        if (entity == root ||
            IsEntityDisabled(registry, entity) ||
            !IsDescendantOf(registry, entity, root)) {
            continue;
        }

        const auto& collider = view.get<JoltColliderComponent>(entity);
        if (collider.shape == JoltColliderShape::Compound) {
            continue;
        }

        const auto& transform = view.get<Transform>(entity);
        const auto childShape = CreatePrimitiveShape(collider, transform);
        if (childShape == nullptr) {
            continue;
        }

        const glm::vec3 localPosition =
            inverseRootRotation * (transform.position - rootTransform.position) + rootOffset;
        const glm::quat localRotation = glm::normalize(
            inverseRootRotation * transform.rotation);
        settings.AddShape(
            ToJoltVec3(localPosition),
            ToJoltQuat(localRotation),
            childShape.GetPtr());
    }

    if (settings.mSubShapes.empty()) {
        return nullptr;
    }

    auto result = settings.Create();
    if (result.HasError()) {
        ENGINE_LOG_ERROR("Failed to create Jolt compound shape: " + result.GetError());
        return nullptr;
    }
    return result.Get();
}

JPH::RefConst<JPH::Shape> CreateShape(
    entt::registry& registry,
    entt::entity entity,
    const JoltColliderComponent& collider,
    const Transform& transform)
{
    if (collider.shape == JoltColliderShape::Compound) {
        return CreateCompoundShape(registry, entity, collider, transform);
    }
    return CreatePrimitiveShape(collider, transform);
}

JPH::EMotionType ToJoltMotionType(JoltBodyMotion motion)
{
    return motion == JoltBodyMotion::Static
        ? JPH::EMotionType::Static
        : JPH::EMotionType::Kinematic;
}

JPH::ObjectLayer ToJoltObjectLayer(JoltBodyMotion motion)
{
    return motion == JoltBodyMotion::Static
        ? Layers::Static
        : Layers::Kinematic;
}

bool ApproximatelyEqual(float first, float second)
{
    return std::abs(first - second) <= 0.0001f;
}

bool ApproximatelyEqual(const glm::vec3& first, const glm::vec3& second)
{
    return
        ApproximatelyEqual(first.x, second.x) &&
        ApproximatelyEqual(first.y, second.y) &&
        ApproximatelyEqual(first.z, second.z);
}

} // namespace

std::vector<glm::vec3> BuildJoltConvexHullPoints(std::span<const glm::vec3> sourcePoints)
{
    const auto shape = CreateConvexHullShape(sourcePoints, glm::vec3{ 1.0f }, true);
    if (shape == nullptr || shape->GetSubType() != JPH::EShapeSubType::ConvexHull) {
        ENGINE_LOG_ERROR("Failed to generate Jolt convex hull points from mesh.");
        return {};
    }

    const auto* hull = static_cast<const JPH::ConvexHullShape*>(shape.GetPtr());
    const glm::vec3 centerOfMass = FromJoltVec3(hull->GetCenterOfMass());
    std::vector<glm::vec3> points;
    points.reserve(hull->GetNumPoints());
    for (JPH::uint index = 0; index < hull->GetNumPoints(); ++index) {
        points.push_back(FromJoltVec3(hull->GetPoint(index)) + centerOfMass);
    }
    return points;
}

std::vector<JoltConvexHullEdge> BuildJoltConvexHullEdges(
    std::span<const glm::vec3> hullPoints)
{
    const auto shape = CreateConvexHullShape(hullPoints, glm::vec3{ 1.0f }, false);
    if (shape == nullptr || shape->GetSubType() != JPH::EShapeSubType::ConvexHull) {
        return {};
    }

    const auto* hull = static_cast<const JPH::ConvexHullShape*>(shape.GetPtr());
    const glm::vec3 centerOfMass = FromJoltVec3(hull->GetCenterOfMass());
    std::unordered_set<uint64_t> visitedEdges;
    std::vector<JoltConvexHullEdge> edges;
    for (JPH::uint faceIndex = 0; faceIndex < hull->GetNumFaces(); ++faceIndex) {
        std::array<JPH::uint, JPH::ConvexHullShape::cMaxPointsInHull> indices{};
        const JPH::uint count = hull->GetFaceVertices(
            faceIndex,
            static_cast<JPH::uint>(indices.size()),
            indices.data());
        for (JPH::uint index = 0; index < count; ++index) {
            const JPH::uint first = indices[index];
            const JPH::uint second = indices[(index + 1) % count];
            const JPH::uint low = std::min(first, second);
            const JPH::uint high = std::max(first, second);
            const uint64_t edgeKey =
                (static_cast<uint64_t>(low) << 32u) | static_cast<uint64_t>(high);
            if (!visitedEdges.insert(edgeKey).second) {
                continue;
            }
            edges.push_back(JoltConvexHullEdge{
                FromJoltVec3(hull->GetPoint(first)) + centerOfMass,
                FromJoltVec3(hull->GetPoint(second)) + centerOfMass
            });
        }
    }
    return edges;
}

JoltPhysicsSystem::JoltPhysicsSystem(entt::registry& registry, Core* core)
    : System(registry)
    , _core(core)
{
    InitializeJolt();
}

JoltPhysicsSystem::~JoltPhysicsSystem()
{
    for (auto& [entity, bodyId] : _bodies) {
        if (!_physicsSystem) {
            continue;
        }

        auto& bodyInterface = _physicsSystem->GetBodyInterface();
        const JPH::BodyID joltBodyId{ bodyId };
        bodyInterface.RemoveBody(joltBodyId);
        bodyInterface.DestroyBody(joltBodyId);
    }
}

void JoltPhysicsSystem::InitializeJolt()
{
    EnsureJoltTypesRegistered();

    constexpr JPH::uint maxBodies = 65536;
    constexpr JPH::uint numBodyMutexes = 0;
    constexpr JPH::uint maxBodyPairs = 65536;
    constexpr JPH::uint maxContactConstraints = 32768;
    constexpr int maxPhysicsJobs = 2048;
    constexpr int maxPhysicsBarriers = 8;

    _tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
    _jobSystem = std::make_unique<JPH::JobSystemThreadPool>(
        maxPhysicsJobs,
        maxPhysicsBarriers,
        std::max(1u, std::thread::hardware_concurrency()) - 1);
    _physicsSystem = std::make_unique<JPH::PhysicsSystem>();
    _physicsSystem->Init(
        maxBodies,
        numBodyMutexes,
        maxBodyPairs,
        maxContactConstraints,
        GetBroadPhaseLayerInterface(),
        GetObjectVsBroadPhaseLayerFilter(),
        GetObjectLayerPairFilter());
    _physicsSystem->SetGravity(JPH::Vec3::sZero());
}

void JoltPhysicsSystem::Update(float deltaTime)
{
    if (!_physicsSystem) {
        return;
    }

    if (_core && !_core->IsPlayMode()) {
        return;
    }

    _physicsSystem->Update(
        std::max(0.0f, deltaTime),
        1,
        _tempAllocator.get(),
        _jobSystem.get());
}

void JoltPhysicsSystem::FixedUpdate(float)
{
    if (!_physicsSystem || (_core && !_core->IsPlayMode())) {
        return;
    }

    SyncRuntimeBodies();
}

void JoltPhysicsSystem::OnPlayStart()
{
    while (!_bodies.empty()) {
        RemoveBody(_bodies.begin()->first);
    }

    InitializeJolt();
    SyncBodies();
}

void JoltPhysicsSystem::OnPlayStop()
{
    while (!_bodies.empty()) {
        RemoveBody(_bodies.begin()->first);
    }

    InitializeJolt();
}

std::vector<JoltPhysicsSystem::ShapeHit> JoltPhysicsSystem::CollideSphereWithStatic(
    const glm::vec3& position,
    float radius) const
{
    std::vector<ShapeHit> hits;
    if (!_physicsSystem || radius <= 0.0f) {
        return hits;
    }

    const JPH::SphereShape sphere{ std::max(0.0001f, radius) };
    JPH::CollideShapeSettings settings;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    _physicsSystem->GetNarrowPhaseQuery().CollideShape(
        &sphere,
        JPH::Vec3::sOne(),
        JPH::RMat44::sTranslation(ToJoltVec3(position)),
        settings,
        JPH::RVec3::sZero(),
        collector,
        {},
        GetStaticObjectLayerFilter());

    hits.reserve(collector.mHits.size());
    for (const auto& hit : collector.mHits) {
        if (hit.mPenetrationDepth <= 0.0f) {
            continue;
        }

        const auto entityIterator =
            _bodyEntities.find(hit.mBodyID2.GetIndexAndSequenceNumber());
        if (entityIterator == _bodyEntities.end()) {
            continue;
        }

        const glm::vec3 normal =
            FromJoltVec3((-hit.mPenetrationAxis).NormalizedOr(JPH::Vec3::sAxisY()));
        hits.push_back(ShapeHit{
            entityIterator->second,
            normal,
            FromJoltVec3(hit.mContactPointOn1),
            hit.mPenetrationDepth
        });
    }

    return hits;
}

void JoltPhysicsSystem::SyncBodies()
{
    BuildCompoundAuthoringCache();
    RemoveStaleBodies();

    auto view = _registry.view<Transform, JoltColliderComponent>(entt::exclude<DisabledEntityTag>);
    for (auto entity : view) {
        if (IsEntityDisabled(_registry, entity)) {
            RemoveBody(entity);
            continue;
        }

        if (_compoundOwners.contains(entity)) {
            RemoveBody(entity);
            continue;
        }

        const auto& collider = view.get<JoltColliderComponent>(entity);
        const uint64_t compoundSignature =
            collider.shape == JoltColliderShape::Compound
                ? _compoundSignatures[entity]
                : 0;
        CreateOrUpdateBody(entity, compoundSignature);
    }
}

void JoltPhysicsSystem::SyncRuntimeBodies()
{
    auto view = _registry.view<Transform, JoltColliderComponent>(
        entt::exclude<DisabledEntityTag>);
    if (!_compoundAuthoringCacheValid || view.size_hint() != _cachedColliderEntityCount) {
        SyncBodies();
        return;
    }

    for (const entt::entity entity : view) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }
        if (!_compoundOwners.contains(entity) && !_bodies.contains(entity)) {
            SyncBodies();
            return;
        }
    }

    RemoveStaleBodies();
    auto& bodyInterface = _physicsSystem->GetBodyInterface();
    for (const auto& [entity, bodyIdValue] : _bodies) {
        if (!_registry.valid(entity) ||
            !_registry.all_of<Transform, JoltColliderComponent>(entity) ||
            IsEntityDisabled(_registry, entity)) {
            continue;
        }

        const auto& collider = _registry.get<JoltColliderComponent>(entity);
        if (collider.motion != JoltBodyMotion::Kinematic) {
            continue;
        }

        const auto& transform = _registry.get<Transform>(entity);
        bodyInterface.SetPositionAndRotationWhenChanged(
            JPH::BodyID{ bodyIdValue },
            ToJoltVec3(transform.position),
            ToJoltQuat(transform.rotation),
            JPH::EActivation::Activate);
    }
}

void JoltPhysicsSystem::BuildCompoundAuthoringCache()
{
    _compoundOwners.clear();
    _compoundSignatures.clear();

    auto view = _registry.view<Transform, JoltColliderComponent>(entt::exclude<DisabledEntityTag>);
    _cachedColliderEntityCount = view.size_hint();
    for (const entt::entity entity : view) {
        if (IsEntityDisabled(_registry, entity)) {
            continue;
        }

        const auto& collider = view.get<JoltColliderComponent>(entity);
        const entt::entity owner = FindOutermostCompoundAncestor(_registry, entity);
        if (owner != entt::null) {
            _compoundOwners.emplace(entity, owner);
            if (collider.shape != JoltColliderShape::Compound) {
                AppendCompoundChildSignature(
                    _registry,
                    owner,
                    entity,
                    _compoundSignatures[owner]);
            }
        }
        else if (collider.shape == JoltColliderShape::Compound) {
            _compoundSignatures.try_emplace(entity, 0);
        }
    }
    _compoundAuthoringCacheValid = true;
}

void JoltPhysicsSystem::RemoveStaleBodies()
{
    for (auto iterator = _bodies.begin(); iterator != _bodies.end();) {
        const entt::entity entity = iterator->first;
        if (_registry.valid(entity) &&
            _registry.all_of<Transform, JoltColliderComponent>(entity) &&
            !IsEntityDisabled(_registry, entity) &&
            !_compoundOwners.contains(entity)) {
            ++iterator;
            continue;
        }

        const JPH::BodyID bodyId{ iterator->second };
        auto& bodyInterface = _physicsSystem->GetBodyInterface();
        bodyInterface.RemoveBody(bodyId);
        bodyInterface.DestroyBody(bodyId);
        _bodyEntities.erase(bodyId.GetIndexAndSequenceNumber());
        if (_registry.valid(entity) && _registry.all_of<JoltBodyComponent>(entity)) {
            _registry.remove<JoltBodyComponent>(entity);
        }
        iterator = _bodies.erase(iterator);
    }
}

void JoltPhysicsSystem::RemoveBody(entt::entity entity)
{
    const auto bodyIterator = _bodies.find(entity);
    if (bodyIterator == _bodies.end()) {
        if (_registry.valid(entity) && _registry.all_of<JoltBodyComponent>(entity)) {
            _registry.remove<JoltBodyComponent>(entity);
        }
        return;
    }

    auto& bodyInterface = _physicsSystem->GetBodyInterface();
    const JPH::BodyID bodyId{ bodyIterator->second };
    bodyInterface.RemoveBody(bodyId);
    bodyInterface.DestroyBody(bodyId);
    _bodyEntities.erase(bodyId.GetIndexAndSequenceNumber());
    _bodies.erase(bodyIterator);

    if (_registry.valid(entity) && _registry.all_of<JoltBodyComponent>(entity)) {
        _registry.remove<JoltBodyComponent>(entity);
    }
}

void JoltPhysicsSystem::CreateOrUpdateBody(
    entt::entity entity,
    uint64_t compoundSignature)
{
    if (!BodyMatchesAuthoring(entity, compoundSignature)) {
        RemoveBody(entity);
    }

    const auto& transform = _registry.get<Transform>(entity);
    const auto& collider = _registry.get<JoltColliderComponent>(entity);

    auto& bodyInterface = _physicsSystem->GetBodyInterface();
    const auto bodyIterator = _bodies.find(entity);
    if (bodyIterator != _bodies.end()) {
        bodyInterface.SetPositionAndRotationWhenChanged(
            JPH::BodyID{ bodyIterator->second },
            ToJoltVec3(transform.position),
            ToJoltQuat(transform.rotation),
            JPH::EActivation::Activate);
        return;
    }

    const auto shape = CreateShape(_registry, entity, collider, transform);
    if (shape == nullptr) {
        return;
    }
    JPH::BodyCreationSettings settings(
        shape,
        ToJoltVec3(transform.position),
        ToJoltQuat(transform.rotation),
        ToJoltMotionType(collider.motion),
        ToJoltObjectLayer(collider.motion));
    settings.mFriction = std::max(0.0f, collider.friction);
    settings.mRestitution = std::max(0.0f, collider.restitution);
    settings.mIsSensor = collider.sensor;
    settings.mUserData = static_cast<JPH::uint64>(entt::to_integral(entity));

    const JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings, JPH::EActivation::Activate);
    if (bodyId.IsInvalid()) {
        ENGINE_LOG_ERROR("Failed to create Jolt body.");
        return;
    }

    _bodies[entity] = bodyId.GetIndexAndSequenceNumber();
    _bodyEntities[bodyId.GetIndexAndSequenceNumber()] = entity;
    _registry.emplace_or_replace<JoltBodyComponent>(
        entity,
        JoltBodyComponent{
            bodyId.GetIndexAndSequenceNumber(),
            collider.shape,
            collider.motion,
            collider.sensor,
            transform.scale,
            collider.center,
            collider.radius,
            collider.halfExtents,
            collider.capsuleHalfHeight,
            collider.cylinderHalfHeight,
            collider.coneHalfHeight,
            collider.friction,
            collider.restitution,
            collider.shape == JoltColliderShape::ConvexHull
                ? ConvexHullSignature(collider)
                : 0,
            collider.shape == JoltColliderShape::Compound
                ? compoundSignature
                : 0
        });
}

bool JoltPhysicsSystem::BodyMatchesAuthoring(
    entt::entity entity,
    uint64_t compoundSignature) const
{
    if (!_registry.all_of<JoltBodyComponent>(entity)) {
        return false;
    }

    const auto& collider = _registry.get<JoltColliderComponent>(entity);
    const auto& transform = _registry.get<Transform>(entity);
    const auto& body = _registry.get<JoltBodyComponent>(entity);

    return
        body.shape == collider.shape &&
        body.motion == collider.motion &&
        body.sensor == collider.sensor &&
        ApproximatelyEqual(body.transformScale, transform.scale) &&
        ApproximatelyEqual(body.center, collider.center) &&
        ApproximatelyEqual(body.radius, collider.radius) &&
        ApproximatelyEqual(body.halfExtents, collider.halfExtents) &&
        ApproximatelyEqual(body.capsuleHalfHeight, collider.capsuleHalfHeight) &&
        ApproximatelyEqual(body.cylinderHalfHeight, collider.cylinderHalfHeight) &&
        ApproximatelyEqual(body.coneHalfHeight, collider.coneHalfHeight) &&
        ApproximatelyEqual(body.friction, collider.friction) &&
        ApproximatelyEqual(body.restitution, collider.restitution) &&
        body.convexHullSignature == (
            collider.shape == JoltColliderShape::ConvexHull
                ? ConvexHullSignature(collider)
                : 0) &&
        body.compoundSignature == (
            collider.shape == JoltColliderShape::Compound
                ? compoundSignature
                : 0);
}

} // namespace Engine
