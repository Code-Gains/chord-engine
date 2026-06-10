#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
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

private:
    struct ComponentSerializer {
        std::string type;
        SaveCallback save;
        LoadCallback load;
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
    nlohmann::json SaveWorldToJson(Core& core) const;
    bool LoadWorldFromJson(Core& core, const nlohmann::json& root) const;

    ComponentSerializerRegistry& ComponentSerializers();
    const ComponentSerializerRegistry& ComponentSerializers() const;

private:
    Serialization::SerializedWorld CaptureWorld(Core& core) const;
    void ApplyWorld(Core& core, const Serialization::SerializedWorld& world) const;
    std::optional<Serialization::SerializedEntity> CaptureEntity(Core& core, entt::entity entity) const;
    entt::entity ApplyEntity(Core& core, const Serialization::SerializedEntity& entity) const;
    void RegisterDefaultComponentSerializers();

    ComponentSerializerRegistry _componentSerializers;
};

} // namespace Engine
