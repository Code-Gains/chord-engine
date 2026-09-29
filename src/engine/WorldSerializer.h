#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

namespace Engine {

class Core;

namespace Serialization {

constexpr uint32_t CurrentWorldVersion = 1;

struct SerializedComponent {
    std::string type;
    nlohmann::json data;
};

struct SerializedEntity {
    uint64_t id = 0;
    std::vector<SerializedComponent> components;
};

struct SerializedEntityHierarchy {
    uint64_t rootId = 0;
    std::vector<SerializedEntity> entities;
};

struct SerializedWorld {
    uint32_t version = CurrentWorldVersion;
    nlohmann::json editor = nlohmann::json::object();
    std::vector<SerializedEntity> entities;
};

} // namespace Serialization

class ComponentSerializerRegistry {
public:
    using SaveCallback = std::function<std::optional<Serialization::SerializedComponent>(
        Core&,
        entt::registry&,
        entt::entity
    )>;

    using LoadCallback = std::function<void(
        Core&,
        entt::registry&,
        entt::entity,
        const Serialization::SerializedComponent&
    )>;

    using RemoveCallback = std::function<void(
        entt::registry&,
        entt::entity
    )>;

    using CollectEntitiesCallback = std::function<void(
        entt::registry&,
        std::unordered_set<entt::entity>&
    )>;

    template<typename RuntimeComponent>
    void Register(
        std::string type,
        std::function<nlohmann::json(Core&, const RuntimeComponent&)> toJson,
        std::function<RuntimeComponent(Core&, const nlohmann::json&)> fromJson
    )
    {
        ComponentSerializer serializer;
        serializer.type = std::move(type);

        serializer.save = [typeName = serializer.type, toJson](
            Core& core,
            entt::registry& registry,
            entt::entity entity
        ) -> std::optional<Serialization::SerializedComponent> {
            if (!registry.all_of<RuntimeComponent>(entity)) {
                return std::nullopt;
            }

            return Serialization::SerializedComponent {
                typeName,
                toJson(core, registry.get<RuntimeComponent>(entity))
            };
        };

        serializer.load = [fromJson](
            Core& core,
            entt::registry& registry,
            entt::entity entity,
            const Serialization::SerializedComponent& component
        ) {
            registry.emplace_or_replace<RuntimeComponent>(
                entity,
                fromJson(core, component.data)
            );
        };

        serializer.remove = [](entt::registry& registry, entt::entity entity) {
            if (registry.all_of<RuntimeComponent>(entity)) {
                registry.remove<RuntimeComponent>(entity);
            }
        };

        serializer.collectEntities = [](
            entt::registry& registry,
            std::unordered_set<entt::entity>& entities
        ) {
            auto view = registry.view<RuntimeComponent>();
            for (auto entity : view) {
                entities.insert(entity);
            }
        };

        _serializers.push_back(std::move(serializer));
    }

    template<typename RuntimeTag>
    void RegisterTag(std::string type)
    {
        ComponentSerializer serializer;
        serializer.type = std::move(type);

        serializer.save = [typeName = serializer.type](
            Core&,
            entt::registry& registry,
            entt::entity entity
        ) -> std::optional<Serialization::SerializedComponent> {
            if (!registry.all_of<RuntimeTag>(entity)) {
                return std::nullopt;
            }

            return Serialization::SerializedComponent {
                typeName,
                nlohmann::json::object()
            };
        };

        serializer.load = [](
            Core&,
            entt::registry& registry,
            entt::entity entity,
            const Serialization::SerializedComponent&
        ) {
            registry.emplace_or_replace<RuntimeTag>(entity);
        };

        serializer.remove = [](entt::registry& registry, entt::entity entity) {
            if (registry.all_of<RuntimeTag>(entity)) {
                registry.remove<RuntimeTag>(entity);
            }
        };

        serializer.collectEntities = [](
            entt::registry& registry,
            std::unordered_set<entt::entity>& entities
        ) {
            auto view = registry.view<RuntimeTag>();
            for (auto entity : view) {
                entities.insert(entity);
            }
        };

        _serializers.push_back(std::move(serializer));
    }

    std::vector<entt::entity> SerializableEntities(entt::registry& registry) const;

    std::vector<Serialization::SerializedComponent> SaveComponents(
        Core& core,
        entt::registry& registry,
        entt::entity entity
    ) const;

    void LoadComponent(
        Core& core,
        entt::registry& registry,
        entt::entity entity,
        const Serialization::SerializedComponent& component
    ) const;

    void ClearComponents(entt::registry& registry, entt::entity entity) const;
    bool RemoveComponent(
        entt::registry& registry,
        entt::entity entity,
        std::string_view type) const;

private:
    struct ComponentSerializer {
        std::string type;
        SaveCallback save;
        LoadCallback load;
        RemoveCallback remove;
        CollectEntitiesCallback collectEntities;
    };

    std::vector<ComponentSerializer> _serializers;
};

class WorldSerializer {
public:
    WorldSerializer();

    bool SaveWorld(Core& core, const std::filesystem::path& path) const;
    bool LoadWorld(Core& core, const std::filesystem::path& path) const;
    bool SavePrefab(Core& core, entt::entity entity, const std::filesystem::path& path) const;
    std::optional<entt::entity> InstantiatePrefab(Core& core, const std::filesystem::path& path) const;
    std::optional<Serialization::SerializedEntity> SerializeEntity(Core& core, entt::entity entity) const;
    entt::entity InstantiateEntity(Core& core, const Serialization::SerializedEntity& entity) const;
    bool RestoreEntity(Core& core, const Serialization::SerializedEntity& entity) const;
    bool RestoreComponent(
        Core& core,
        entt::entity entity,
        const Serialization::SerializedComponent& component) const;
    bool RemoveComponent(Core& core, entt::entity entity, std::string_view type) const;
    std::optional<Serialization::SerializedEntityHierarchy> SerializeEntityHierarchy(
        Core& core,
        entt::entity root) const;
    std::optional<entt::entity> InstantiateEntityHierarchy(
        Core& core,
        const Serialization::SerializedEntityHierarchy& hierarchy,
        bool preserveSerializedIds = false,
        bool preserveExternalParent = false) const;
    nlohmann::json SaveWorldToJson(Core& core) const;
    bool LoadWorldFromJson(Core& core, const nlohmann::json& root) const;

    ComponentSerializerRegistry& ComponentSerializers();
    const ComponentSerializerRegistry& ComponentSerializers() const;

private:
    Serialization::SerializedWorld CaptureWorld(Core& core) const;
    void ApplyWorld(Core& core, const Serialization::SerializedWorld& world) const;
    std::optional<Serialization::SerializedEntity> CaptureEntity(Core& core, entt::entity entity) const;
    std::vector<Serialization::SerializedEntity> CaptureEntityHierarchy(Core& core, entt::entity root) const;
    entt::entity ApplyEntity(
        Core& core,
        const Serialization::SerializedEntity& entity,
        bool preserveSerializedId = false) const;
    std::optional<entt::entity> ApplyPrefab(
        Core& core,
        uint64_t rootId,
        const std::vector<Serialization::SerializedEntity>& entities,
        bool preserveSerializedIds = false,
        bool preserveExternalParent = false) const;
    void RegisterDefaultComponentSerializers();

    ComponentSerializerRegistry _componentSerializers;
};

} // namespace Engine
