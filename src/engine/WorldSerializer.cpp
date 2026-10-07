#include "WorldSerializer.h"

#include "Camera.h"
#include "Core.h"
#include "EnvironmentComponent.h"
#include "GravityComponents.h"
#include "HierarchyComponent.h"
#include "HeightFogComponent.h"
#include "EntityState.h"
#include "JoltPhysicsComponents.h"
#include "Log.h"
#include "MeshComponent.h"
#include "NameComponent.h"
#include "PrefabComponents.h"
#include "ScreenPostProcessComponent.h"
#include "SunlightComponent.h"
#include "Transform.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <glm/trigonometric.hpp>
#include <unordered_map>

namespace Engine {
namespace {

struct SerializedPrefab {
    uint64_t rootId = 0;
    std::vector<Serialization::SerializedEntity> entities;
};

nlohmann::json Vec3ToJson(const glm::vec3& value)
{
    return {
        value.x,
        value.y,
        value.z
    };
}

glm::vec3 Vec3FromJson(const nlohmann::json& value)
{
    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>()
    };
}

nlohmann::json Vec4ToJson(const glm::vec4& value)
{
    return {
        value.x,
        value.y,
        value.z,
        value.w
    };
}

glm::vec4 Vec4FromJson(const nlohmann::json& value)
{
    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
        value.at(3).get<float>()
    };
}

nlohmann::json QuatToJson(const glm::quat& value)
{
    return {
        value.w,
        value.x,
        value.y,
        value.z
    };
}

glm::quat QuatFromJson(const nlohmann::json& value)
{
    return {
        value.at(0).get<float>(),
        value.at(1).get<float>(),
        value.at(2).get<float>(),
        value.at(3).get<float>()
    };
}

const char* CameraBackgroundModeToString(CameraBackgroundMode mode)
{
    switch (mode) {
    case CameraBackgroundMode::None:
        return "None";
    case CameraBackgroundMode::SolidColor:
        return "SolidColor";
    case CameraBackgroundMode::Skybox:
        return "Skybox";
    default:
        return "Skybox";
    }
}

const char* PrefabPlacementRotationModeToString(PrefabPlacementRotationMode mode)
{
    switch (mode) {
    case PrefabPlacementRotationMode::Preserve:
        return "Preserve";
    case PrefabPlacementRotationMode::AlignToPlacementUp:
        return "AlignToPlacementUp";
    case PrefabPlacementRotationMode::Automatic:
    default:
        return "Automatic";
    }
}

PrefabPlacementRotationMode PrefabPlacementRotationModeFromString(std::string_view value)
{
    if (value == "Preserve") {
        return PrefabPlacementRotationMode::Preserve;
    }
    if (value == "AlignToPlacementUp") {
        return PrefabPlacementRotationMode::AlignToPlacementUp;
    }
    return PrefabPlacementRotationMode::Automatic;
}

CameraBackgroundMode CameraBackgroundModeFromString(const std::string& value)
{
    if (value == "None") {
        return CameraBackgroundMode::None;
    }

    if (value == "SolidColor") {
        return CameraBackgroundMode::SolidColor;
    }

    return CameraBackgroundMode::Skybox;
}

const char* CameraShotInterpolationModeToString(CameraShotInterpolationMode mode)
{
    switch (mode) {
    case CameraShotInterpolationMode::Linear:
        return "Linear";
    case CameraShotInterpolationMode::Smoothstep:
        return "Smoothstep";
    case CameraShotInterpolationMode::CatmullRom:
        return "CatmullRom";
    default:
        return "CatmullRom";
    }
}

CameraShotInterpolationMode CameraShotInterpolationModeFromString(const std::string& value)
{
    if (value == "Linear") {
        return CameraShotInterpolationMode::Linear;
    }

    if (value == "Smoothstep") {
        return CameraShotInterpolationMode::Smoothstep;
    }

    return CameraShotInterpolationMode::CatmullRom;
}

const char* CameraShotAimModeToString(CameraShotAimMode mode)
{
    switch (mode) {
    case CameraShotAimMode::UseRotation:
        return "UseRotation";
    case CameraShotAimMode::LookAtPoint:
        return "LookAtPoint";
    case CameraShotAimMode::LookAtEntity:
        return "LookAtEntity";
    default:
        return "UseRotation";
    }
}

CameraShotAimMode CameraShotAimModeFromString(const std::string& value)
{
    if (value == "LookAtPoint") {
        return CameraShotAimMode::LookAtPoint;
    }

    if (value == "LookAtEntity") {
        return CameraShotAimMode::LookAtEntity;
    }

    return CameraShotAimMode::UseRotation;
}

const char* JoltColliderShapeToString(JoltColliderShape shape)
{
    switch (shape) {
    case JoltColliderShape::Sphere:
        return "Sphere";
    case JoltColliderShape::Box:
        return "Box";
    case JoltColliderShape::Capsule:
        return "Capsule";
    case JoltColliderShape::Cylinder:
        return "Cylinder";
    case JoltColliderShape::Cone:
        return "Cone";
    case JoltColliderShape::ConvexHull:
        return "ConvexHull";
    case JoltColliderShape::Compound:
        return "Compound";
    default:
        return "Sphere";
    }
}

JoltColliderShape JoltColliderShapeFromString(const std::string& value)
{
    if (value == "Box") {
        return JoltColliderShape::Box;
    }

    if (value == "Capsule") {
        return JoltColliderShape::Capsule;
    }

    if (value == "Cylinder") {
        return JoltColliderShape::Cylinder;
    }

    if (value == "Cone") {
        return JoltColliderShape::Cone;
    }

    if (value == "ConvexHull") {
        return JoltColliderShape::ConvexHull;
    }

    if (value == "Compound") {
        return JoltColliderShape::Compound;
    }

    return JoltColliderShape::Sphere;
}

const char* JoltBodyMotionToString(JoltBodyMotion motion)
{
    switch (motion) {
    case JoltBodyMotion::Static:
        return "Static";
    case JoltBodyMotion::Kinematic:
        return "Kinematic";
    default:
        return "Kinematic";
    }
}

JoltBodyMotion JoltBodyMotionFromString(const std::string& value)
{
    if (value == "Static") {
        return JoltBodyMotion::Static;
    }

    return JoltBodyMotion::Kinematic;
}

float ResolveCameraShotDuration(const CinematicCameraShotComponent& shot)
{
    float duration = 0.0f;
    for (std::size_t index = 1; index < shot.keyframes.size(); ++index) {
        duration += glm::max(0.0f, shot.keyframes[index].duration);
    }

    return duration;
}

entt::entity EntityFromSerializedId(uint64_t id)
{
    return static_cast<entt::entity>(id);
}

nlohmann::json WorldToJson(const Serialization::SerializedWorld& world)
{
    nlohmann::json entities = nlohmann::json::array();

    for (const auto& entity : world.entities) {
        nlohmann::json components = nlohmann::json::array();

        for (const auto& component : entity.components) {
            components.push_back({
                {"type", component.type},
                {"data", component.data}
            });
        }

        entities.push_back({
            {"id", entity.id},
            {"components", components}
        });
    }

    nlohmann::json root = {
        {"version", world.version},
        {"entities", entities}
    };

    if (!world.editor.empty()) {
        root["editor"] = world.editor;
    }

    return root;
}

nlohmann::json PrefabToJson(
    uint64_t rootId,
    const std::vector<Serialization::SerializedEntity>& entities)
{
    nlohmann::json serializedEntities = nlohmann::json::array();

    for (const auto& entity : entities) {
        nlohmann::json components = nlohmann::json::array();

        for (const auto& component : entity.components) {
            components.push_back({
                {"type", component.type},
                {"data", component.data}
            });
        }

        serializedEntities.push_back({
            {"id", entity.id},
            {"components", components}
        });
    }

    return {
        {"version", Serialization::CurrentWorldVersion},
        {"root", rootId},
        {"entities", serializedEntities}
    };
}

Serialization::SerializedWorld WorldFromJson(const nlohmann::json& root)
{
    Serialization::SerializedWorld world;
    world.version = root.at("version").get<uint32_t>();
    world.editor = root.value("editor", nlohmann::json::object());

    for (const auto& entityJson : root.at("entities")) {
        Serialization::SerializedEntity entity;
        entity.id = entityJson.at("id").get<uint64_t>();

        for (const auto& componentJson : entityJson.at("components")) {
            entity.components.push_back({
                componentJson.at("type").get<std::string>(),
                componentJson.at("data")
            });
        }

        world.entities.push_back(std::move(entity));
    }

    return world;
}

Serialization::SerializedEntity EntityFromJson(const nlohmann::json& entityJson)
{
    Serialization::SerializedEntity entity;
    entity.id = entityJson.value("id", uint64_t{ 0 });

    for (const auto& componentJson : entityJson.at("components")) {
        entity.components.push_back({
            componentJson.at("type").get<std::string>(),
            componentJson.at("data")
        });
    }

    return entity;
}

SerializedPrefab PrefabFromJson(const nlohmann::json& root)
{
    const uint32_t version = root.at("version").get<uint32_t>();
    if (version != Serialization::CurrentWorldVersion) {
        throw std::runtime_error("Unsupported prefab version: " + std::to_string(version));
    }

    if (root.contains("entity")) {
        auto entity = EntityFromJson(root.at("entity"));
        return SerializedPrefab{
            entity.id,
            { std::move(entity) }
        };
    }

    auto world = WorldFromJson(root);
    if (world.entities.empty()) {
        throw std::runtime_error("Prefab must contain at least one entity.");
    }

    const uint64_t rootId = root.value("root", world.entities.front().id);
    return SerializedPrefab{
        rootId,
        std::move(world.entities)
    };
}

nlohmann::json CaptureEditorState(Core& core)
{
    auto& registry = core.GetRegistry();
    auto cameraView = registry.view<Camera, Transform, ActiveCameraTag, CoreOwnedTag>();

    if (cameraView.begin() == cameraView.end()) {
        return nlohmann::json::object();
    }

    const auto cameraEntity = *cameraView.begin();
    const auto& transform = cameraView.get<Transform>(cameraEntity);

    return {
        {"camera", {
            {"position", Vec3ToJson(transform.position)},
            {"rotation", QuatToJson(transform.rotation)}
        }}
    };
}

void ApplyEditorState(Core& core, const nlohmann::json& editorState)
{
    if (!editorState.contains("camera")) {
        return;
    }

    auto& registry = core.GetRegistry();
    auto cameraView = registry.view<Camera, Transform, ActiveCameraTag, CoreOwnedTag>();

    if (cameraView.begin() == cameraView.end()) {
        return;
    }

    const auto cameraEntity = *cameraView.begin();
    auto& camera = cameraView.get<Camera>(cameraEntity);
    auto& transform = cameraView.get<Transform>(cameraEntity);
    const auto& cameraState = editorState.at("camera");

    transform.position = Vec3FromJson(cameraState.at("position"));
    transform.rotation = QuatFromJson(cameraState.at("rotation"));

    camera.direction = glm::normalize(transform.rotation * glm::vec3{ 0.0f, 0.0f, -1.0f });
    camera.pitch = glm::degrees(asin(glm::clamp(camera.direction.y, -1.0f, 1.0f)));
    camera.yaw = glm::degrees(atan2(camera.direction.z, camera.direction.x));
}

} // namespace

std::vector<entt::entity> ComponentSerializerRegistry::SerializableEntities(
    entt::registry& registry
) const
{
    std::unordered_set<entt::entity> uniqueEntities;

    for (const auto& serializer : _serializers) {
        serializer.collectEntities(registry, uniqueEntities);
    }

    return {
        uniqueEntities.begin(),
        uniqueEntities.end()
    };
}

std::vector<Serialization::SerializedComponent> ComponentSerializerRegistry::SaveComponents(
    Core& core,
    entt::registry& registry,
    entt::entity entity
) const
{
    std::vector<Serialization::SerializedComponent> components;

    for (const auto& serializer : _serializers) {
        auto component = serializer.save(core, registry, entity);
        if (component.has_value()) {
            components.push_back(std::move(component.value()));
        }
    }

    return components;
}

void ComponentSerializerRegistry::LoadComponent(
    Core& core,
    entt::registry& registry,
    entt::entity entity,
    const Serialization::SerializedComponent& component
) const
{
    for (const auto& serializer : _serializers) {
        if (serializer.type == component.type) {
            try {
                serializer.load(core, registry, entity, component);
            } catch (const std::exception& exception) {
                ENGINE_LOG_ERROR(
                    "Failed to load component " +
                    component.type +
                    ": " +
                    exception.what()
                );
            }
            return;
        }
    }

    ENGINE_LOG_WARN("No component serializer registered for: " + component.type);
}

void ComponentSerializerRegistry::ClearComponents(
    entt::registry& registry,
    entt::entity entity) const
{
    for (const auto& serializer : _serializers) {
        serializer.remove(registry, entity);
    }
}

bool ComponentSerializerRegistry::RemoveComponent(
    entt::registry& registry,
    entt::entity entity,
    std::string_view type) const
{
    for (const auto& serializer : _serializers) {
        if (serializer.type == type) {
            serializer.remove(registry, entity);
            return true;
        }
    }
    return false;
}

WorldSerializer::WorldSerializer()
{
    RegisterDefaultComponentSerializers();
}

bool WorldSerializer::SaveWorld(Core& core, const std::filesystem::path& path) const
{
    std::ofstream output(path);
    if (!output.is_open()) {
        ENGINE_LOG_ERROR("Failed to open world file for saving: " + path.string());
        return false;
    }

    auto world = CaptureWorld(core);
    output << WorldToJson(world).dump(4);

    return true;
}

bool WorldSerializer::LoadWorld(Core& core, const std::filesystem::path& path) const
{
    std::ifstream input(path);
    if (!input.is_open()) {
        ENGINE_LOG_ERROR("Failed to open world file for loading: " + path.string());
        return false;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Failed to parse world file: " + std::string(exception.what()));
        return false;
    }

    Serialization::SerializedWorld world;
    try {
        world = WorldFromJson(root);
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Invalid world file: " + std::string(exception.what()));
        return false;
    }

    if (world.version != Serialization::CurrentWorldVersion) {
        ENGINE_LOG_ERROR("Unsupported world version: " + std::to_string(world.version));
        return false;
    }

    ApplyWorld(core, world);
    return true;
}

bool WorldSerializer::SavePrefab(Core& core, entt::entity entity, const std::filesystem::path& path) const
{
    auto serializedEntities = CaptureEntityHierarchy(core, entity);
    if (serializedEntities.empty()) {
        ENGINE_LOG_ERROR("Failed to capture prefab hierarchy.");
        return false;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        ENGINE_LOG_ERROR("Failed to open prefab file for saving: " + path.string());
        return false;
    }

    output << PrefabToJson(
        static_cast<uint64_t>(entt::to_integral(entity)),
        serializedEntities).dump(4);
    return true;
}

std::optional<entt::entity> WorldSerializer::InstantiatePrefab(
    Core& core,
    const std::filesystem::path& path) const
{
    std::ifstream input(path);
    if (!input.is_open()) {
        ENGINE_LOG_ERROR("Failed to open prefab file for loading: " + path.string());
        return std::nullopt;
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Failed to parse prefab file: " + std::string(exception.what()));
        return std::nullopt;
    }

    SerializedPrefab prefab;
    try {
        prefab = PrefabFromJson(root);
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Invalid prefab file: " + std::string(exception.what()));
        return std::nullopt;
    }

    return ApplyPrefab(core, prefab.rootId, prefab.entities);
}

std::optional<Serialization::SerializedEntity> WorldSerializer::SerializeEntity(
    Core& core,
    entt::entity entity) const
{
    return CaptureEntity(core, entity);
}

entt::entity WorldSerializer::InstantiateEntity(
    Core& core,
    const Serialization::SerializedEntity& entity) const
{
    return ApplyEntity(core, entity);
}

bool WorldSerializer::RestoreEntity(
    Core& core,
    const Serialization::SerializedEntity& serializedEntity) const
{
    auto& registry = core.GetRegistry();
    const entt::entity entity = EntityFromSerializedId(serializedEntity.id);
    if (!registry.valid(entity) || registry.all_of<CoreOwnedTag>(entity)) {
        return false;
    }

    _componentSerializers.ClearComponents(registry, entity);
    for (const auto& component : serializedEntity.components) {
        _componentSerializers.LoadComponent(core, registry, entity, component);
    }
    return true;
}

bool WorldSerializer::RestoreComponent(
    Core& core,
    entt::entity entity,
    const Serialization::SerializedComponent& component) const
{
    auto& registry = core.GetRegistry();
    if (!registry.valid(entity) || registry.all_of<CoreOwnedTag>(entity)) {
        return false;
    }

    _componentSerializers.LoadComponent(core, registry, entity, component);
    return true;
}

bool WorldSerializer::RemoveComponent(
    Core& core,
    entt::entity entity,
    std::string_view type) const
{
    auto& registry = core.GetRegistry();
    if (!registry.valid(entity) || registry.all_of<CoreOwnedTag>(entity)) {
        return false;
    }
    return _componentSerializers.RemoveComponent(registry, entity, type);
}

std::optional<Serialization::SerializedEntityHierarchy> WorldSerializer::SerializeEntityHierarchy(
    Core& core,
    entt::entity root) const
{
    auto entities = CaptureEntityHierarchy(core, root);
    if (entities.empty()) {
        return std::nullopt;
    }

    return Serialization::SerializedEntityHierarchy {
        static_cast<uint64_t>(entt::to_integral(root)),
        std::move(entities)
    };
}

std::optional<entt::entity> WorldSerializer::InstantiateEntityHierarchy(
    Core& core,
    const Serialization::SerializedEntityHierarchy& hierarchy,
    bool preserveSerializedIds,
    bool preserveExternalParent) const
{
    return ApplyPrefab(
        core,
        hierarchy.rootId,
        hierarchy.entities,
        preserveSerializedIds,
        preserveExternalParent);
}

nlohmann::json WorldSerializer::SaveWorldToJson(Core& core) const
{
    return WorldToJson(CaptureWorld(core));
}

bool WorldSerializer::LoadWorldFromJson(Core& core, const nlohmann::json& root) const
{
    Serialization::SerializedWorld world;
    try {
        world = WorldFromJson(root);
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Invalid world JSON: " + std::string(exception.what()));
        return false;
    }

    if (world.version != Serialization::CurrentWorldVersion) {
        ENGINE_LOG_ERROR("Unsupported world version: " + std::to_string(world.version));
        return false;
    }

    ApplyWorld(core, world);
    return true;
}

ComponentSerializerRegistry& WorldSerializer::ComponentSerializers()
{
    return _componentSerializers;
}

const ComponentSerializerRegistry& WorldSerializer::ComponentSerializers() const
{
    return _componentSerializers;
}

Serialization::SerializedWorld WorldSerializer::CaptureWorld(Core& core) const
{
    Serialization::SerializedWorld world;
    auto& registry = core.GetRegistry();

    world.editor = CaptureEditorState(core);

    for (auto entity : _componentSerializers.SerializableEntities(registry)) {
        if (registry.all_of<CoreOwnedTag>(entity)) {
            continue;
        }

        auto serializedEntity = CaptureEntity(core, entity);
        if (serializedEntity.has_value()) {
            world.entities.push_back(std::move(serializedEntity.value()));
        }
    }

    return world;
}

void WorldSerializer::ApplyWorld(Core& core, const Serialization::SerializedWorld& world) const
{
    auto& registry = core.GetRegistry();
    auto entities = _componentSerializers.SerializableEntities(registry);

    for (auto entity : entities) {
        if (!registry.valid(entity) || registry.all_of<CoreOwnedTag>(entity)) {
            continue;
        }

        registry.destroy(entity);
    }

    for (const auto& serializedEntity : world.entities) {
        ApplyEntity(core, serializedEntity, true);
    }

    ApplyEditorState(core, world.editor);
}

std::optional<Serialization::SerializedEntity> WorldSerializer::CaptureEntity(
    Core& core,
    entt::entity entity) const
{
    auto& registry = core.GetRegistry();
    if (!registry.valid(entity) || registry.all_of<CoreOwnedTag>(entity)) {
        return std::nullopt;
    }

    auto components = _componentSerializers.SaveComponents(core, registry, entity);
    if (components.empty()) {
        return std::nullopt;
    }

    return Serialization::SerializedEntity {
        static_cast<uint64_t>(entt::to_integral(entity)),
        std::move(components)
    };
}

std::vector<Serialization::SerializedEntity> WorldSerializer::CaptureEntityHierarchy(
    Core& core,
    entt::entity root) const
{
    std::vector<Serialization::SerializedEntity> entities;
    auto& registry = core.GetRegistry();
    if (!registry.valid(root) || registry.all_of<CoreOwnedTag>(root)) {
        return entities;
    }

    std::vector<entt::entity> pending{ root };
    std::unordered_set<entt::entity> visited;

    for (std::size_t index = 0; index < pending.size(); ++index) {
        const entt::entity entity = pending[index];
        if (!registry.valid(entity) ||
            registry.all_of<CoreOwnedTag>(entity) ||
            visited.contains(entity)) {
            continue;
        }

        visited.insert(entity);

        auto serializedEntity = CaptureEntity(core, entity);
        if (serializedEntity.has_value()) {
            entities.push_back(std::move(serializedEntity.value()));
        }

        auto hierarchyView = registry.view<HierarchyComponent>();
        for (auto child : hierarchyView) {
            if (visited.contains(child) || registry.all_of<CoreOwnedTag>(child)) {
                continue;
            }

            const auto& hierarchy = hierarchyView.get<HierarchyComponent>(child);
            if (hierarchy.parent == entity) {
                pending.push_back(child);
            }
        }
    }

    return entities;
}

entt::entity WorldSerializer::ApplyEntity(
    Core& core,
    const Serialization::SerializedEntity& serializedEntity,
    bool preserveSerializedId) const
{
    auto& registry = core.GetRegistry();
    entt::entity entity = entt::null;

    if (preserveSerializedId && serializedEntity.id != 0) {
        const entt::entity preferredEntity = EntityFromSerializedId(serializedEntity.id);
        entity = registry.valid(preferredEntity)
            ? registry.create()
            : registry.create(preferredEntity);
    }
    else {
        entity = registry.create();
    }

    for (const auto& component : serializedEntity.components) {
        _componentSerializers.LoadComponent(core, registry, entity, component);
    }

    return entity;
}

std::optional<entt::entity> WorldSerializer::ApplyPrefab(
    Core& core,
    uint64_t rootId,
    const std::vector<Serialization::SerializedEntity>& entities,
    bool preserveSerializedIds,
    bool preserveExternalParent) const
{
    if (entities.empty()) {
        return std::nullopt;
    }

    auto& registry = core.GetRegistry();
    std::unordered_map<uint64_t, entt::entity> remappedEntities;
    std::optional<entt::entity> rootEntity;

    for (const auto& serializedEntity : entities) {
        entt::entity entity = entt::null;
        if (preserveSerializedIds && serializedEntity.id != 0) {
            const entt::entity preferredEntity = EntityFromSerializedId(serializedEntity.id);
            entity = registry.valid(preferredEntity)
                ? registry.create()
                : registry.create(preferredEntity);
        }
        else {
            entity = registry.create();
        }
        remappedEntities[serializedEntity.id] = entity;
        if (serializedEntity.id == rootId) {
            rootEntity = entity;
        }
    }

    if (!rootEntity.has_value()) {
        rootEntity = remappedEntities[entities.front().id];
    }

    for (const auto& serializedEntity : entities) {
        const entt::entity entity = remappedEntities.at(serializedEntity.id);
        for (const auto& component : serializedEntity.components) {
            _componentSerializers.LoadComponent(core, registry, entity, component);
        }
    }

    for (const auto& [serializedId, entity] : remappedEntities) {
        auto* hierarchy = registry.try_get<HierarchyComponent>(entity);
        if (!hierarchy || hierarchy->parent == entt::null) {
            continue;
        }

        const uint64_t serializedParentId = static_cast<uint64_t>(entt::to_integral(hierarchy->parent));
        const auto parentIt = remappedEntities.find(serializedParentId);
        if (parentIt != remappedEntities.end()) {
            hierarchy->parent = parentIt->second;
        }
        else if ((!preserveSerializedIds && !preserveExternalParent) ||
            !registry.valid(hierarchy->parent)) {
            hierarchy->parent = entt::null;
        }
    }

    return rootEntity;
}

void WorldSerializer::RegisterDefaultComponentSerializers()
{
    _componentSerializers.Register<Transform>(
        "Transform",
        [](Core&, const Transform& transform) {
            return nlohmann::json {
                {"position", Vec3ToJson(transform.position)},
                {"rotation", QuatToJson(transform.rotation)},
                {"scale", Vec3ToJson(transform.scale)}
            };
        },
        [](Core&, const nlohmann::json& data) {
            Transform transform;
            transform.position = Vec3FromJson(data.at("position"));
            transform.rotation = QuatFromJson(data.at("rotation"));
            transform.scale = Vec3FromJson(data.at("scale"));
            return transform;
        }
    );

    _componentSerializers.Register<HierarchyComponent>(
        "HierarchyComponent",
        [](Core&, const HierarchyComponent& hierarchy) {
            const bool hasParent = hierarchy.parent != entt::null;
            return nlohmann::json {
                {"parent", hasParent ? static_cast<uint64_t>(entt::to_integral(hierarchy.parent)) : uint64_t{ 0 }},
                {"inheritTransform", hierarchy.inheritTransform},
                {"localPosition", Vec3ToJson(hierarchy.localTransform.position)},
                {"localRotation", QuatToJson(hierarchy.localTransform.rotation)},
                {"localScale", Vec3ToJson(hierarchy.localTransform.scale)}
            };
        },
        [](Core&, const nlohmann::json& data) {
            HierarchyComponent hierarchy;
            const uint64_t parentId = data.value("parent", uint64_t{ 0 });
            hierarchy.parent = parentId == 0 ? entt::null : EntityFromSerializedId(parentId);
            hierarchy.inheritTransform = data.value("inheritTransform", true);
            hierarchy.localTransform.position = Vec3FromJson(data.at("localPosition"));
            hierarchy.localTransform.rotation = QuatFromJson(data.at("localRotation"));
            hierarchy.localTransform.scale = Vec3FromJson(data.at("localScale"));
            return hierarchy;
        }
    );

    _componentSerializers.Register<NameComponent>(
        "NameComponent",
        [](Core&, const NameComponent& name) {
            return nlohmann::json {
                {"name", name.name}
            };
        },
        [](Core&, const nlohmann::json& data) {
            return NameComponent {
                data.at("name").get<std::string>()
            };
        }
    );

    _componentSerializers.Register<MeshComponent>(
        "MeshComponent",
        [](Core&, const MeshComponent& meshComponent) {
            MeshAssetReference reference = meshComponent.source;

            if (!reference.IsValid() && meshComponent.mesh) {
                reference = meshComponent.mesh->source;
            }

            auto data = nlohmann::json {
                {"path", reference.path},
                {"meshIndex", reference.meshIndex},
                {"baseColorFactor", Vec4ToJson(meshComponent.baseColorFactor)}
            };

            if (!meshComponent.materialOverride.empty()) {
                data["materialOverride"] = meshComponent.materialOverride;
            }

            return data;
        },
        [](Core& core, const nlohmann::json& data) {
            MeshComponent meshComponent;
            meshComponent.source = MeshAssetReference {
                data.at("path").get<std::string>(),
                data.at("meshIndex").get<uint32_t>()
            };
            meshComponent.baseColorFactor =
                data.contains("baseColorFactor")
                    ? Vec4FromJson(data.at("baseColorFactor"))
                    : glm::vec4{ 1.0f };
            meshComponent.materialOverride =
                data.value("materialOverride", std::string{});

            auto meshes = core.LoadGltfMeshes(&core, meshComponent.source.path);
            if (!meshes.has_value()) {
                ENGINE_LOG_ERROR("Failed to load serialized mesh asset: " + meshComponent.source.path);
                return meshComponent;
            }

            if (meshComponent.source.meshIndex >= meshes->size()) {
                ENGINE_LOG_ERROR("Serialized mesh index is out of range for asset: " + meshComponent.source.path);
                return meshComponent;
            }

            meshComponent.mesh = meshes->at(meshComponent.source.meshIndex);
            return meshComponent;
        }
    );

    _componentSerializers.Register<EffectMeshComponent>(
        "EffectMeshComponent",
        [](Core&, const EffectMeshComponent& effect) {
            return nlohmann::json {
                {"color", Vec4ToJson(effect.color)},
                {"corruptionColor", Vec4ToJson(effect.corruptionColor)},
                {"velocity", Vec3ToJson(effect.velocity)},
                {"angularVelocity", Vec3ToJson(effect.angularVelocity)},
                {"lifetime", effect.lifetime},
                {"age", effect.age},
                {"startScale", effect.startScale},
                {"endScale", effect.endScale},
                {"fresnelPower", effect.fresnelPower},
                {"fresnelIntensity", effect.fresnelIntensity},
                {"baseIntensity", effect.baseIntensity},
                {"corruptionScale", effect.corruptionScale},
                {"corruptionSoftness", effect.corruptionSoftness},
                {"corruptionIntensity", effect.corruptionIntensity},
                {"corruptionAmount", effect.corruptionAmount},
                {"destroyOnComplete", effect.destroyOnComplete}
            };
        },
        [](Core&, const nlohmann::json& data) {
            EffectMeshComponent effect;
            effect.color = Vec4FromJson(data.at("color"));
            effect.corruptionColor = Vec4FromJson(data.at("corruptionColor"));
            effect.velocity = Vec3FromJson(data.at("velocity"));
            effect.angularVelocity = Vec3FromJson(data.at("angularVelocity"));
            effect.lifetime = data.value("lifetime", 0.35f);
            effect.age = data.value("age", 0.0f);
            effect.startScale = data.value("startScale", 1.0f);
            effect.endScale = data.value("endScale", 2.0f);
            effect.fresnelPower = data.value("fresnelPower", 2.5f);
            effect.fresnelIntensity = data.value("fresnelIntensity", 2.0f);
            effect.baseIntensity = data.value("baseIntensity", 0.2f);
            effect.corruptionScale = data.value("corruptionScale", 8.0f);
            effect.corruptionSoftness = data.value("corruptionSoftness", 0.04f);
            effect.corruptionIntensity = data.value("corruptionIntensity", 0.0f);
            effect.corruptionAmount = data.value("corruptionAmount", 0.0f);
            effect.destroyOnComplete = data.value("destroyOnComplete", true);
            return effect;
        }
    );

    _componentSerializers.Register<MeshCorruptionComponent>(
        "MeshCorruptionComponent",
        [](Core&, const MeshCorruptionComponent& corruption) {
            return nlohmann::json {
                {"color", Vec4ToJson(corruption.color)},
                {"scale", corruption.scale},
                {"softness", corruption.softness},
                {"intensity", corruption.intensity},
                {"amount", corruption.amount},
                {"age", corruption.age},
                {"speed", corruption.speed}
            };
        },
        [](Core&, const nlohmann::json& data) {
            MeshCorruptionComponent corruption;
            corruption.color = data.contains("color")
                ? Vec4FromJson(data.at("color"))
                : corruption.color;
            corruption.scale = data.value("scale", corruption.scale);
            corruption.softness = data.value("softness", corruption.softness);
            corruption.intensity = data.value("intensity", corruption.intensity);
            corruption.amount = data.value("amount", corruption.amount);
            corruption.age = data.value("age", corruption.age);
            corruption.speed = data.value("speed", corruption.speed);
            return corruption;
        }
    );

    _componentSerializers.Register<HeightFogComponent>(
        "HeightFogComponent",
        [](Core&, const HeightFogComponent& fog) {
            return nlohmann::json {
                {"enabled", fog.enabled},
                {"debugOverlay", fog.debugOverlay},
                {"color", Vec3ToJson(fog.color)},
                {"planetRadius", fog.planetRadius},
                {"height", fog.height},
                {"density", fog.density},
                {"distanceFalloff", fog.distanceFalloff},
                {"maxOpacity", fog.maxOpacity},
                {"skyDensityMultiplier", fog.skyDensityMultiplier},
                {"skyMaxOpacity", fog.skyMaxOpacity}
            };
        },
        [](Core&, const nlohmann::json& data) {
            HeightFogComponent fog;
            fog.enabled = data.value("enabled", true);
            fog.debugOverlay = data.value("debugOverlay", false);
            fog.color = data.contains("color")
                ? Vec3FromJson(data.at("color"))
                : fog.color;
            fog.planetRadius = data.value("planetRadius", fog.planetRadius);
            fog.height = data.value("height", fog.height);
            fog.density = data.value("density", fog.density);
            fog.distanceFalloff = data.value("distanceFalloff", fog.distanceFalloff);
            fog.maxOpacity = data.value("maxOpacity", fog.maxOpacity);
            fog.skyDensityMultiplier = data.value("skyDensityMultiplier", fog.skyDensityMultiplier);
            fog.skyMaxOpacity = data.value("skyMaxOpacity", fog.skyMaxOpacity);
            return fog;
        }
    );

    _componentSerializers.Register<ScreenPostProcessComponent>(
        "ScreenPostProcessComponent",
        [](Core&, const ScreenPostProcessComponent& effect) {
            return nlohmann::json {
                {"enabled", effect.enabled},
                {"debugOverlay", effect.debugOverlay},
                {"useSourceMask", effect.useSourceMask},
                {"color", Vec3ToJson(effect.color)},
                {"scale", effect.scale},
                {"softness", effect.softness},
                {"intensity", effect.intensity},
                {"amount", effect.amount},
                {"displacement", effect.displacement},
                {"chromaticAberration", effect.chromaticAberration},
                {"blockSize", effect.blockSize},
                {"maskFeather", effect.maskFeather},
                {"speed", effect.speed},
                {"age", effect.age}
            };
        },
        [](Core&, const nlohmann::json& data) {
            ScreenPostProcessComponent effect;
            effect.enabled = data.value("enabled", effect.enabled);
            effect.debugOverlay = data.value("debugOverlay", effect.debugOverlay);
            effect.useSourceMask = data.value(
                "useSourceMask",
                data.value("useScreenRadius", data.value("useWorldRadius", effect.useSourceMask)));
            effect.color = data.contains("color")
                ? Vec3FromJson(data.at("color"))
                : effect.color;
            effect.scale = data.value("scale", effect.scale);
            effect.softness = data.value("softness", effect.softness);
            effect.intensity = data.value("intensity", effect.intensity);
            effect.amount = data.value("amount", effect.amount);
            effect.displacement = data.value("displacement", effect.displacement);
            effect.chromaticAberration = data.value("chromaticAberration", effect.chromaticAberration);
            effect.blockSize = data.value("blockSize", effect.blockSize);
            effect.maskFeather = data.value("maskFeather", data.value("screenFeather", effect.maskFeather));
            effect.speed = data.value("speed", effect.speed);
            effect.age = data.value("age", effect.age);
            return effect;
        }
    );

    _componentSerializers.Register<ScreenPostProcessSourceComponent>(
        "ScreenPostProcessSourceComponent",
        [](Core&, const ScreenPostProcessSourceComponent& source) {
            return nlohmann::json {
                {"enabled", source.enabled},
                {"worldRadiusExtension", source.worldRadiusExtension}
            };
        },
        [](Core&, const nlohmann::json& data) {
            ScreenPostProcessSourceComponent source;
            source.enabled = data.value("enabled", source.enabled);
            source.worldRadiusExtension = data.value("worldRadiusExtension", source.worldRadiusExtension);
            return source;
        }
    );

    _componentSerializers.RegisterTag<SingleRenderTag>("SingleRenderTag");
    _componentSerializers.Register<PrefabPlacementAnchorComponent>(
        "PrefabPlacementAnchorComponent",
        [](Core&, const PrefabPlacementAnchorComponent& anchor) {
            return nlohmann::json {
                {"rotationMode", PrefabPlacementRotationModeToString(anchor.rotationMode)},
                {"footprintRadius", anchor.footprintRadius}
            };
        },
        [](Core&, const nlohmann::json& data) {
            PrefabPlacementAnchorComponent anchor;
            anchor.rotationMode = PrefabPlacementRotationModeFromString(
                data.value("rotationMode", std::string("Automatic")));
            anchor.footprintRadius = std::max(0.0f, data.value("footprintRadius", 0.0f));
            return anchor;
        });
    _componentSerializers.RegisterTag<ActiveCameraTag>("ActiveCameraTag");
    _componentSerializers.RegisterTag<DisabledEntityTag>("DisabledEntityTag");

    _componentSerializers.Register<EnvironmentComponent>(
        "EnvironmentComponent",
        [](Core&, const EnvironmentComponent& environment) {
            return nlohmann::json {
                {"skyboxPath", environment.skyboxPath}
            };
        },
        [](Core&, const nlohmann::json& data) {
            EnvironmentComponent environment;
            environment.skyboxPath = data.value("skyboxPath", environment.skyboxPath);
            return environment;
        }
    );

    _componentSerializers.Register<Camera>(
        "Camera",
        [](Core&, const Camera& camera) {
            return nlohmann::json {
                {"fov", camera.fov},
                {"nearPlane", camera.nearPlane},
                {"farPlane", camera.farPlane},
                {"backgroundMode", CameraBackgroundModeToString(camera.backgroundMode)},
                {"clearColor", Vec4ToJson(camera.clearColor)},
                {"speed", camera.speed}
            };
        },
        [](Core&, const nlohmann::json& data) {
            Camera camera;
            camera.fov = data.at("fov").get<float>();
            camera.nearPlane = data.at("nearPlane").get<float>();
            camera.farPlane = data.at("farPlane").get<float>();
            camera.backgroundMode = CameraBackgroundModeFromString(
                data.value("backgroundMode", std::string{ CameraBackgroundModeToString(camera.backgroundMode) }));
            if (data.contains("clearColor")) {
                camera.clearColor = Vec4FromJson(data.at("clearColor"));
            }
            camera.speed = data.value("speed", camera.speed);
            return camera;
        }
    );

    _componentSerializers.Register<CinematicCameraShotComponent>(
        "CinematicCameraShotComponent",
        [](Core&, const CinematicCameraShotComponent& shot) {
            nlohmann::json keyframes = nlohmann::json::array();
            for (const auto& keyframe : shot.keyframes) {
                keyframes.push_back({
                    {"duration", keyframe.duration},
                    {"position", Vec3ToJson(keyframe.position)},
                    {"rotation", QuatToJson(keyframe.rotation)},
                    {"fov", keyframe.fov},
                    {"interpolationMode", CameraShotInterpolationModeToString(keyframe.interpolationMode)},
                    {"aimMode", CameraShotAimModeToString(keyframe.aimMode)},
                    {"lookAtPoint", Vec3ToJson(keyframe.lookAtPoint)},
                    {"lookAtEntity", keyframe.lookAtEntity == entt::null
                        ? uint64_t{ 0 }
                        : static_cast<uint64_t>(entt::to_integral(keyframe.lookAtEntity))}
                });
            }

            return nlohmann::json {
                {"duration", ResolveCameraShotDuration(shot)},
                {"time", shot.time},
                {"playbackSpeed", shot.playbackSpeed},
                {"playing", shot.playing},
                {"loop", shot.loop},
                {"showPath", shot.showPath},
                {"interpolationMode", CameraShotInterpolationModeToString(shot.interpolationMode)},
                {"keyframes", keyframes}
            };
        },
        [](Core&, const nlohmann::json& data) {
            CinematicCameraShotComponent shot;
            shot.duration = data.value("duration", 3.0f);
            shot.time = data.value("time", 0.0f);
            shot.playbackSpeed = data.value("playbackSpeed", 1.0f);
            shot.playing = data.value("playing", false);
            shot.loop = data.value("loop", false);
            shot.showPath = data.value("showPath", true);
            if (data.contains("interpolationMode")) {
                shot.interpolationMode = CameraShotInterpolationModeFromString(
                    data.value("interpolationMode", "CatmullRom"));
            }
            else {
                shot.interpolationMode = data.value("smoothInterpolation", true)
                    ? CameraShotInterpolationMode::Smoothstep
                    : CameraShotInterpolationMode::Linear;
            }

            bool loadedAbsoluteKeyframeTimes = false;
            if (data.contains("keyframes") && data["keyframes"].is_array()) {
                for (const auto& keyframeJson : data["keyframes"]) {
                    CameraShotKeyframe keyframe;
                    if (keyframeJson.contains("duration")) {
                        keyframe.duration = keyframeJson.value("duration", 1.0f);
                    }
                    else {
                        keyframe.duration = keyframeJson.value("time", 0.0f);
                        loadedAbsoluteKeyframeTimes = true;
                    }
                    keyframe.position = Vec3FromJson(keyframeJson.at("position"));
                    keyframe.rotation = QuatFromJson(keyframeJson.at("rotation"));
                    keyframe.fov = keyframeJson.value("fov", 90.0f);
                    keyframe.interpolationMode = keyframeJson.contains("interpolationMode")
                        ? CameraShotInterpolationModeFromString(
                            keyframeJson.value("interpolationMode", "CatmullRom"))
                        : shot.interpolationMode;
                    keyframe.aimMode = CameraShotAimModeFromString(
                        keyframeJson.value("aimMode", "UseRotation"));
                    keyframe.lookAtPoint = keyframeJson.contains("lookAtPoint")
                        ? Vec3FromJson(keyframeJson.at("lookAtPoint"))
                        : glm::vec3{ 0.0f };
                    const uint64_t lookAtEntityId =
                        keyframeJson.value("lookAtEntity", uint64_t{ 0 });
                    keyframe.lookAtEntity = lookAtEntityId == 0
                        ? entt::null
                        : EntityFromSerializedId(lookAtEntityId);
                    shot.keyframes.push_back(keyframe);
                }
            }

            if (loadedAbsoluteKeyframeTimes) {
                std::sort(
                    shot.keyframes.begin(),
                    shot.keyframes.end(),
                    [](const CameraShotKeyframe& first, const CameraShotKeyframe& second) {
                        return first.duration < second.duration;
                    });

                float previousAbsoluteTime = 0.0f;
                for (std::size_t index = 0; index < shot.keyframes.size(); ++index) {
                    const float absoluteTime = glm::max(0.0f, shot.keyframes[index].duration);
                    shot.keyframes[index].duration = index == 0
                        ? 0.0f
                        : glm::max(0.0f, absoluteTime - previousAbsoluteTime);
                    previousAbsoluteTime = absoluteTime;
                }
            }
            else if (!shot.keyframes.empty()) {
                shot.keyframes.front().duration = 0.0f;
            }

            shot.duration = ResolveCameraShotDuration(shot);

            return shot;
        }
    );

    _componentSerializers.Register<SunlightComponent>(
        "SunlightComponent",
        [](Core&, const SunlightComponent& sunlight) {
            return nlohmann::json {
                {"direction", Vec3ToJson(sunlight.direction)},
                {"intensity", sunlight.intensity},
                {"color", Vec3ToJson(sunlight.color)},
                {"ambient", sunlight.ambient}
            };
        },
        [](Core&, const nlohmann::json& data) {
            SunlightComponent sunlight;
            sunlight.direction = Vec3FromJson(data.at("direction"));
            sunlight.intensity = data.at("intensity").get<float>();
            sunlight.color = Vec3FromJson(data.at("color"));
            sunlight.ambient = data.at("ambient").get<float>();
            return sunlight;
        }
    );

    _componentSerializers.Register<VelocityComponent>(
        "VelocityComponent",
        [](Core&, const VelocityComponent& velocity) {
            return nlohmann::json {
                {"linear", Vec3ToJson(velocity.linear)},
                {"angular", Vec3ToJson(velocity.angular)}
            };
        },
        [](Core&, const nlohmann::json& data) {
            VelocityComponent velocity;
            velocity.linear = Vec3FromJson(data.at("linear"));
            velocity.angular = Vec3FromJson(data.at("angular"));
            return velocity;
        }
    );

    _componentSerializers.Register<GravityBodyComponent>(
        "GravityBodyComponent",
        [](Core&, const GravityBodyComponent& gravityBody) {
            return nlohmann::json {
                {"mass", gravityBody.mass},
                {"affectedByGravity", gravityBody.affectedByGravity}
            };
        },
        [](Core&, const nlohmann::json& data) {
            GravityBodyComponent gravityBody;
            gravityBody.mass = data.at("mass").get<float>();
            gravityBody.affectedByGravity = data.value("affectedByGravity", true);
            return gravityBody;
        }
    );

    _componentSerializers.Register<GravityParticleComponent>(
        "GravityParticleComponent",
        [](Core&, const GravityParticleComponent& gravityParticle) {
            return nlohmann::json {
                {"gravityScale", gravityParticle.gravityScale}
            };
        },
        [](Core&, const nlohmann::json& data) {
            GravityParticleComponent gravityParticle;
            gravityParticle.gravityScale = data.at("gravityScale").get<float>();
            return gravityParticle;
        }
    );

    _componentSerializers.Register<JoltColliderComponent>(
        "JoltColliderComponent",
        [](Core&, const JoltColliderComponent& collider) {
            nlohmann::json convexHullPoints = nlohmann::json::array();
            for (const glm::vec3& point : collider.convexHullPoints) {
                convexHullPoints.push_back(Vec3ToJson(point));
            }
            return nlohmann::json {
                {"shape", JoltColliderShapeToString(collider.shape)},
                {"motion", JoltBodyMotionToString(collider.motion)},
                {"sensor", collider.sensor},
                {"center", Vec3ToJson(collider.center)},
                {"radius", collider.radius},
                {"halfExtents", Vec3ToJson(collider.halfExtents)},
                {"capsuleHalfHeight", collider.capsuleHalfHeight},
                {"cylinderHalfHeight", collider.cylinderHalfHeight},
                {"coneHalfHeight", collider.coneHalfHeight},
                {"convexHullPoints", convexHullPoints},
                {"friction", collider.friction},
                {"restitution", collider.restitution}
            };
        },
        [](Core&, const nlohmann::json& data) {
            JoltColliderComponent collider;
            collider.shape = JoltColliderShapeFromString(data.value("shape", std::string{ "Sphere" }));
            collider.motion = JoltBodyMotionFromString(data.value("motion", std::string{ "Kinematic" }));
            collider.sensor = data.value("sensor", false);
            collider.center = data.contains("center")
                ? Vec3FromJson(data.at("center"))
                : glm::vec3{ 0.0f };
            collider.radius = data.value("radius", 1.0f);
            collider.halfExtents = data.contains("halfExtents")
                ? Vec3FromJson(data.at("halfExtents"))
                : glm::vec3{ 0.5f };
            collider.capsuleHalfHeight = data.value("capsuleHalfHeight", 0.5f);
            collider.cylinderHalfHeight = data.value("cylinderHalfHeight", 0.5f);
            collider.coneHalfHeight = data.value("coneHalfHeight", 0.5f);
            if (data.contains("convexHullPoints") && data.at("convexHullPoints").is_array()) {
                for (const auto& point : data.at("convexHullPoints")) {
                    collider.convexHullPoints.push_back(Vec3FromJson(point));
                }
            }
            collider.friction = data.value("friction", 0.2f);
            collider.restitution = data.value("restitution", 0.0f);
            return collider;
        }
    );
}

} // namespace Engine
