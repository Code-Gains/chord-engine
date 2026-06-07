#include "WorldSerializer.h"

#include "Camera.h"
#include "Core.h"
#include "GravityComponents.h"
#include "Log.h"
#include "MeshComponent.h"
#include "NameComponent.h"
#include "SunlightComponent.h"
#include "Transform.h"

#include <exception>
#include <fstream>

namespace Engine {
namespace {

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

    return {
        {"version", world.version},
        {"entities", entities}
    };
}

nlohmann::json PrefabToJson(const Serialization::SerializedEntity& entity)
{
    nlohmann::json components = nlohmann::json::array();

    for (const auto& component : entity.components) {
        components.push_back({
            {"type", component.type},
            {"data", component.data}
        });
    }

    return {
        {"version", Serialization::CurrentWorldVersion},
        {"entity", {
            {"id", entity.id},
            {"components", components}
        }}
    };
}

Serialization::SerializedWorld WorldFromJson(const nlohmann::json& root)
{
    Serialization::SerializedWorld world;
    world.version = root.at("version").get<uint32_t>();

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

Serialization::SerializedEntity PrefabFromJson(const nlohmann::json& root)
{
    const uint32_t version = root.at("version").get<uint32_t>();
    if (version != Serialization::CurrentWorldVersion) {
        throw std::runtime_error("Unsupported prefab version: " + std::to_string(version));
    }

    if (root.contains("entity")) {
        return EntityFromJson(root.at("entity"));
    }

    auto world = WorldFromJson(root);
    if (world.entities.size() != 1) {
        throw std::runtime_error("Prefab world must contain exactly one entity.");
    }

    return std::move(world.entities.front());
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
    auto serializedEntity = CaptureEntity(core, entity);
    if (!serializedEntity.has_value()) {
        ENGINE_LOG_ERROR("Failed to capture prefab entity.");
        return false;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        ENGINE_LOG_ERROR("Failed to open prefab file for saving: " + path.string());
        return false;
    }

    output << PrefabToJson(serializedEntity.value()).dump(4);
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

    Serialization::SerializedEntity entity;
    try {
        entity = PrefabFromJson(root);
    } catch (const std::exception& exception) {
        ENGINE_LOG_ERROR("Invalid prefab file: " + std::string(exception.what()));
        return std::nullopt;
    }

    return ApplyEntity(core, entity);
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
        ApplyEntity(core, serializedEntity);
    }
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

entt::entity WorldSerializer::ApplyEntity(
    Core& core,
    const Serialization::SerializedEntity& serializedEntity) const
{
    auto& registry = core.GetRegistry();
    auto entity = registry.create();

    for (const auto& component : serializedEntity.components) {
        _componentSerializers.LoadComponent(core, registry, entity, component);
    }

    return entity;
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

            return nlohmann::json {
                {"path", reference.path},
                {"meshIndex", reference.meshIndex}
            };
        },
        [](Core& core, const nlohmann::json& data) {
            MeshComponent meshComponent;
            meshComponent.source = MeshAssetReference {
                data.at("path").get<std::string>(),
                data.at("meshIndex").get<uint32_t>()
            };

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

    _componentSerializers.RegisterTag<SingleRenderTag>("SingleRenderTag");
    _componentSerializers.RegisterTag<ActiveCameraTag>("ActiveCameraTag");

    _componentSerializers.Register<Camera>(
        "Camera",
        [](Core&, const Camera& camera) {
            return nlohmann::json {
                {"fov", camera.fov},
                {"aspectRatio", camera.aspectRatio},
                {"nearPlane", camera.nearPlane},
                {"farPlane", camera.farPlane},
                {"clearColor", Vec4ToJson(camera.clearColor)},
                {"speed", camera.speed}
            };
        },
        [](Core&, const nlohmann::json& data) {
            Camera camera;
            camera.fov = data.at("fov").get<float>();
            camera.aspectRatio = data.at("aspectRatio").get<float>();
            camera.nearPlane = data.at("nearPlane").get<float>();
            camera.farPlane = data.at("farPlane").get<float>();
            camera.clearColor = Vec4FromJson(data.at("clearColor"));
            camera.speed = data.at("speed").get<float>();
            return camera;
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
                {"mass", gravityBody.mass}
            };
        },
        [](Core&, const nlohmann::json& data) {
            GravityBodyComponent gravityBody;
            gravityBody.mass = data.at("mass").get<float>();
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
}

} // namespace Engine
