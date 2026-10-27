#include "Core.h"
#include "Camera.h"
#include "SunlightComponent.h"
#include "WorldSerializer.h"
#include "PointLighting.h"
#include "PointShadowMath.h"
#include "SpotLighting.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>

static_assert(offsetof(Engine::GPUSceneData, ambientFillColor) == 112);
static_assert(offsetof(Engine::GPUSceneData, environmentIntensity) == 128);
static_assert(offsetof(Engine::GPUSceneData, pointLightCounts) == 144);
static_assert(offsetof(Engine::GPUSceneData, pointLights) == 160);
static_assert(sizeof(Engine::GPUPointLight) == 32);
static_assert(offsetof(Engine::GPUSceneData, pointShadowParameters) == 672);
static_assert(sizeof(Engine::GPUSpotLight) == 64);
static_assert(offsetof(Engine::GPUSceneData, spotLightCounts) == 688);
static_assert(offsetof(Engine::GPUSceneData, spotLights) == 704);
static_assert(offsetof(Engine::GPUSceneData, spotLightViewProjection) == 1216);
static_assert(offsetof(Engine::GPUSceneData, spotShadowParameters) == 1280);
static_assert(sizeof(Engine::GPUSceneData) == 1296);
static_assert(offsetof(Engine::InstanceData, emissionOverride) == 112);
static_assert(sizeof(Engine::InstanceData) == 128);
static_assert(offsetof(Engine::BatchDrawPushConstants, materialEmission) == 80);
static_assert(offsetof(Engine::BatchDrawPushConstants, vertexBuffer) == 96);
static_assert(sizeof(Engine::BatchDrawPushConstants) == 112);
static_assert(offsetof(GPUDrawPushConstants, materialEmission) == 192);
static_assert(offsetof(GPUDrawPushConstants, emissionOverride) == 208);
static_assert(offsetof(GPUDrawPushConstants, vertexBuffer) == 224);
static_assert(offsetof(GPUDrawPushConstants, surfacePaintReceiver) == 232);
static_assert(sizeof(GPUDrawPushConstants) == 240);
static_assert(offsetof(Engine::InstanceData, surfacePaintReceiver) == 12);

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    Engine::Core core;
    Engine::WorldSerializer world;
    auto& serializers = world.ComponentSerializers();
    entt::registry registry;
    const auto legacy = registry.create();
    serializers.LoadComponent(core, registry, legacy, {
        "SunlightComponent", {
            {"direction", {0.0f, 1.0f, 0.0f}}, {"intensity", 3.0f},
            {"color", {0.8f, 0.6f, 0.4f}}, {"ambient", 0.05f}
        }
    });
    auto& light = registry.get<SunlightComponent>(legacy);
    Check(light.castShadows, "legacy suns must retain enabled shadows");
    Check(light.ambientColor == light.color, "legacy ambient color must preserve sun tint");
    Check(light.environmentDiffuseIntensity == 0.25f && light.environmentSpecularIntensity == 1.0f,
        "legacy environment strengths must match shaders");
    light.ambientColor = {0.1f, 0.2f, 0.3f};
    light.castShadows = false;
    light.ambient = 0.12f;
    light.environmentDiffuseIntensity = 0.8f;
    light.environmentSpecularIntensity = 0.4f;
    const auto saved = serializers.SaveComponents(core, registry, legacy);
    const auto restored = registry.create();
    serializers.LoadComponent(core, registry, restored, saved[0]);
    const auto& copy = registry.get<SunlightComponent>(restored);
    Check(!copy.castShadows, "disabled sun shadows must round trip");
    Check(copy.color == light.color && copy.ambientColor == light.ambientColor && copy.ambient == light.ambient,
        "independent sun and ambient values must round trip");
    Check(copy.environmentDiffuseIntensity == 0.8f && copy.environmentSpecularIntensity == 0.4f,
        "environment strengths must round trip");
    auto invalid = saved[0];
    invalid.data["environmentDiffuseIntensity"] = -2.0f;
    invalid.data["environmentSpecularIntensity"] = -1.0f;
    serializers.LoadComponent(core, registry, restored, invalid);
    const auto& clamped = registry.get<SunlightComponent>(restored);
    Check(clamped.environmentDiffuseIntensity == 0.0f && clamped.environmentSpecularIntensity == 0.0f,
        "negative environment strengths must clamp");
    const auto cameraEntity = registry.create();
    const auto postEntity = registry.create();
    auto& post = registry.emplace<PostProcessComponent>(postEntity);
    post.bloomEnabled = false;
    post.bloomIntensity = 0.7f;
    post.bloomThreshold = 3.0f;
    post.bloomSoftKnee = 0.25f;
    const auto postSaved = serializers.SaveComponents(core, registry, postEntity);
    const auto postRestored = registry.create();
    serializers.LoadComponent(core, registry, postRestored, postSaved[0]);
    const auto& postCopy = registry.get<PostProcessComponent>(postRestored);
    Check(!postCopy.bloomEnabled && postCopy.bloomIntensity == 0.7f && postCopy.bloomThreshold == 3.0f &&
        postCopy.bloomSoftKnee == 0.25f, "bloom settings must round trip");
    serializers.LoadComponent(core, registry, postRestored, {"PostProcessComponent", nlohmann::json::object()});
    Check(registry.get<PostProcessComponent>(postRestored).bloomIntensity == 0.15f, "bloom default settings must load");
    post.bloomIntensity = -1.0f;
    post.bloomThreshold = std::numeric_limits<float>::quiet_NaN();
    post.bloomSoftKnee = 2.0f;
    post.Sanitize();
    Check(post.bloomIntensity == 0.0f && post.bloomThreshold == 1.0f && post.bloomSoftKnee == 1.0f,
        "invalid bloom settings must not reach shaders");
    const auto levels = BuildBloomExtents({1920, 1080});
    Check(levels.size() == 6 && levels.front().width == 960 && levels.back().height == 17,
        "bloom must use a bounded half-resolution pyramid");
    const auto smallLevels = BuildBloomExtents({1, 1});
    Check(smallLevels.size() == 1 && smallLevels[0].width == 1 && smallLevels[0].height == 1,
        "tiny bloom images must remain valid");
    Check(BuildBloomExtents({0, 0}).empty(), "zero extent must not allocate bloom images");
    serializers.LoadComponent(core, registry, cameraEntity, {
        "Camera", {{"fov", 90.0f}, {"nearPlane", 0.1f}, {"farPlane", 1000.0f}}
    });
    Check(registry.get<Camera>(cameraEntity).exposure == 1.0f, "legacy cameras retain unit exposure");
    registry.get<Camera>(cameraEntity).exposure = 2.5f;
    const auto cameraSaved = serializers.SaveComponents(core, registry, cameraEntity);
    const auto restoredCamera = registry.create();
    serializers.LoadComponent(core, registry, restoredCamera, cameraSaved[0]);
    Check(registry.get<Camera>(restoredCamera).exposure == 2.5f, "camera exposure must round trip");
    auto invalidCamera = cameraSaved[0];
    invalidCamera.data["exposure"] = -1.0f;
    serializers.LoadComponent(core, registry, restoredCamera, invalidCamera);
    Check(registry.get<Camera>(restoredCamera).exposure == 0.001f, "negative exposure must clamp");
    invalidCamera.data["exposure"] = 100.0f;
    serializers.LoadComponent(core, registry, restoredCamera, invalidCamera);
    Check(registry.get<Camera>(restoredCamera).exposure == 64.0f, "excessive exposure must clamp");
    const auto pointEntity = registry.create();
    serializers.LoadComponent(core, registry, pointEntity, {"PointLightComponent", {
        {"color", {1.0f, 0.5f, 0.2f}}, {"intensity", 40.0f}, {"range", 12.0f}, {"enabled", false},
        {"castShadows", true}, {"shadowBias", 0.03f}}});
    const auto pointSaved = serializers.SaveComponents(core, registry, pointEntity);
    const auto restoredPoint = registry.create();
    serializers.LoadComponent(core, registry, restoredPoint, pointSaved[0]);
    const auto& point = registry.get<PointLightComponent>(restoredPoint);
    Check(point.color == glm::vec3(1.0f, 0.5f, 0.2f) && point.intensity == 40.0f &&
        point.range == 12.0f && !point.enabled, "point light settings must round trip");
    Check(point.castShadows && point.shadowBias == 0.03f, "shadow settings must round trip");
    auto invalidPoint = pointSaved[0];
    invalidPoint.data["intensity"] = -2.0f;
    invalidPoint.data["range"] = -4.0f;
    serializers.LoadComponent(core, registry, restoredPoint, invalidPoint);
    Check(registry.get<PointLightComponent>(restoredPoint).range == 0.0f &&
        registry.get<PointLightComponent>(restoredPoint).intensity == 0.0f, "negative light settings clamp");

    entt::registry lights;
    const auto spotEntity = registry.create();
    auto& spot = registry.emplace<SpotLightComponent>(spotEntity);
    registry.emplace<Transform>(spotEntity);
    spot.color = {0.2f, 0.6f, 1.0f};
    spot.intensity = 90.0f;
    spot.innerAngle = 10.0f;
    spot.outerAngle = 40.0f;
    const auto spotSaved = serializers.SaveComponents(core, registry, spotEntity);
    const auto spotRestored = registry.create();
    for (const auto& serialized : spotSaved) serializers.LoadComponent(core, registry, spotRestored, serialized);
    const auto& spotCopy = registry.get<SpotLightComponent>(spotRestored);
    Check(spotCopy.color == spot.color && spotCopy.innerAngle == 10 && spotCopy.outerAngle == 40 &&
        spotCopy.intensity == 90 && spotCopy.castShadows, "spotlight settings must round trip");
    auto spotData = Engine::CollectSpotLights(registry);
    Check(spotData.count == 2 && spotData.shadowLightIndex == 0, "spotlights must choose one deterministic shadow slot");
    Check(glm::vec3(spotData.lights[0].directionAndOuterCos) == glm::vec3(0,0,-1), "spotlights aim along local negative Z");
    registry.get<Transform>(spotEntity).rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0,1,0));
    spotData = Engine::CollectSpotLights(registry);
    Check(glm::length(glm::vec3(spotData.lights[0].directionAndOuterCos) - glm::vec3(-1,0,0)) < 0.00001f,
        "spotlight direction must follow transform rotation");
    registry.emplace<DisabledEntityTag>(spotEntity);
    Check(Engine::CollectSpotLights(registry).count == 1, "disabled spotlights must not upload");
    spot.outerAngle = 100.0f;
    spot.innerAngle = 110.0f;
    spot.Sanitize();
    Check(spot.outerAngle == 85 && spot.innerAngle < spot.outerAngle, "cone limits must remain valid for perspective shadows");
    const auto spotMatrix = Engine::BuildSpotShadowMatrix({0,0,0}, {0,0,-1}, 30, 20);
    auto spotClip = spotMatrix * glm::vec4(0,0,-5,1);
    const auto spotNdc = glm::vec3(spotClip) / spotClip.w;
    Check(std::abs(spotNdc.x) < 0.00001f && std::abs(spotNdc.y) < 0.00001f && spotNdc.z > 0 && spotNdc.z < 1,
        "spotlight axis must project into the center of its reversed-depth map");
    const auto outsideClip = spotMatrix * glm::vec4(5,0,-5,1);
    Check(outsideClip.x / outsideClip.w > 1, "positions beyond outer cone must leave shadow coverage");
    const auto upMatrix = Engine::BuildSpotShadowMatrix({0,0,0}, {0,1,0}, 30, 20);
    Check(std::isfinite(upMatrix[0][0]), "vertical spotlights must have a nonsingular view basis");
    entt::registry spotCap;
    for (unsigned int i = 0; i < 12; ++i) {
        auto entity = spotCap.create();
        spotCap.emplace<Transform>(entity);
        spotCap.emplace<SpotLightComponent>(entity);
    }
    Check(Engine::CollectSpotLights(spotCap).count == 8 && Engine::CollectSpotLights(spotCap).eligibleCount == 12,
        "spotlight upload must obey its bounded cap");
    for (unsigned int i = 0; i < 20; ++i) {
        auto entity = lights.create();
        lights.emplace<Transform>(entity).position = glm::vec3(float(i), 2.0f, 3.0f);
        lights.emplace<PointLightComponent>(entity);
    }
    auto packed = Engine::CollectPointLights(lights);
    Check(packed.count == 16 && packed.eligibleCount == 20, "point light upload must respect cap");
    Check(packed.shadowLightIndex == -1, "point lights default to unshadowed");
    lights.get<PointLightComponent>(entt::entity{17}).castShadows = true;
    Check(Engine::CollectPointLights(lights).shadowLightIndex == -1, "lights beyond cap cannot occupy shadow slot");
    lights.get<PointLightComponent>(entt::entity{5}).castShadows = true;
    lights.get<PointLightComponent>(entt::entity{6}).castShadows = true;
    Check(Engine::CollectPointLights(lights).shadowLightIndex == 5, "one deterministic shadow slot");
    Check(packed.lights[0].positionAndRange == glm::vec4(0, 2, 3, 10), "point position and range packing");
    auto first = entt::entity{0};
    lights.get<PointLightComponent>(first).enabled = false;
    lights.emplace<DisabledEntityTag>(entt::entity{1});
    lights.get<PointLightComponent>(entt::entity{2}).intensity = 0;
    lights.get<PointLightComponent>(entt::entity{3}).range = 0;
    packed = Engine::CollectPointLights(lights);
    Check(packed.count == 16 && packed.eligibleCount == 16 && packed.lights[0].positionAndRange.x == 4,
        "disabled and zero-contribution lights must be excluded deterministically");
    lights.clear();
    const auto parent = lights.create();
    lights.emplace<DisabledEntityTag>(parent);
    const auto child = lights.create();
    lights.emplace<Transform>(child);
    lights.emplace<PointLightComponent>(child);
    lights.emplace<HierarchyComponent>(child).parent = parent;
    Check(Engine::CollectPointLights(lights).count == 0, "disabled parents must suppress lights");
    lights.remove<DisabledEntityTag>(parent);
    Check(Engine::CollectPointLights(lights).count == 1, "enabled parents restore lights");
    lights.get<PointLightComponent>(child).intensity = std::numeric_limits<float>::quiet_NaN();
    Check(Engine::CollectPointLights(lights).count == 0, "invalid values must not reach shaders");
    lights.clear();
    packed = Engine::CollectPointLights(lights);
    Check(packed.count == 0 && packed.lights[0].colorAndIntensity == glm::vec4(0), "removed lights must clear upload");
    const glm::vec3 origin{10.0f, 20.0f, 30.0f};
    const float nearPlane = 0.05f;
    const float farPlane = 10.0f;
    const auto matrices = Engine::BuildPointShadowMatrices(origin, nearPlane, farPlane);
    const std::array<glm::vec3, 6> directions{{{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}}};
    for (size_t face = 0; face < 6; ++face) {
        const auto offset = directions[face] * 3.0f + glm::vec3(0.2f, -0.3f, 0.1f);
        const float major = std::max({std::abs(offset.x), std::abs(offset.y), std::abs(offset.z)});
        const std::array<glm::vec2, 6> cubeCoordinates{{{-offset.z,-offset.y}, {offset.z,-offset.y},
            {offset.x,offset.z}, {offset.x,-offset.z}, {offset.x,-offset.y}, {-offset.x,-offset.y}}};
        const auto clip = matrices[face] * glm::vec4(origin + offset, 1.0f);
        const auto ndc = glm::vec3(clip) / clip.w;
        Check(glm::length(glm::vec2(ndc) - cubeCoordinates[face] / major) < 0.0001f,
            "shadow cube view orientation must match sampler coordinates");
        const float sampledDepth = nearPlane * (farPlane - major) / (major * (farPlane - nearPlane));
        Check(std::abs(ndc.z - sampledDepth) < 0.00001f, "receiver depth must match reversed cube projection");
        Check(Engine::PointShadowSphereVisible(origin + directions[face] * 3.0f, 0.1f, matrices[face]),
            "cube frustum must retain facing sphere");
        Check(!Engine::PointShadowSphereVisible(origin - directions[face] * 3.0f, 0.1f, matrices[face]),
            "cube frustum must reject sphere behind face");
    }
    const auto fixture = std::filesystem::path{__FILE__}.parent_path() / "fixtures/emissive_material.gltf";
    auto asset = core.LoadGltfAsset(fixture);
    Check(asset.has_value() && asset->materials.size() == 2, "emission fixture must parse");
    Check(asset->materials[0].emissiveFactor[0] == 0 && asset->materials[0].emissiveStrength == 1,
        "glTF defaults must not emit");
    Check(asset->materials[1].emissiveFactor[1] == 0.5f && asset->materials[1].emissiveStrength == 8,
        "glTF emissive strength extension must be enabled");
    const auto meshEntity = registry.create();
    auto& mesh = registry.emplace<MeshComponent>(meshEntity);
    mesh.source.path = "__lighting_test_missing_mesh__.gltf";
    Check(mesh.GetEmissiveOverride() == glm::vec4(0), "legacy meshes default to no added emission");
    mesh.emissiveColor = glm::vec3(1.0f, 0.5f, 0.25f);
    mesh.emissiveStrength = 12.0f;
    mesh.emissiveUseTexture = true;
    Check(mesh.GetEmissiveOverride() == glm::vec4(12, 6, 3, 1), "HDR emissive packing must not saturate");
    const auto meshSaved = serializers.SaveComponents(core, registry, meshEntity);
    const auto restoredMesh = registry.create();
    serializers.LoadComponent(core, registry, restoredMesh, meshSaved[0]);
    const auto& meshCopy = registry.get<MeshComponent>(restoredMesh);
    Check(meshCopy.GetEmissiveOverride() == mesh.GetEmissiveOverride(), "mesh emission must round trip");
    auto invalidMesh = meshSaved[0];
    invalidMesh.data["emissiveStrength"] = -1.0f;
    serializers.LoadComponent(core, registry, restoredMesh, invalidMesh);
    Check(registry.get<MeshComponent>(restoredMesh).emissiveStrength == 0, "negative emission must clamp");
    auto legacyMesh = meshSaved[0];
    legacyMesh.data.erase("emissiveColor");
    legacyMesh.data.erase("emissiveStrength");
    legacyMesh.data.erase("emissiveUseTexture");
    serializers.LoadComponent(core, registry, restoredMesh, legacyMesh);
    Check(registry.get<MeshComponent>(restoredMesh).GetEmissiveOverride() == glm::vec4(0),
        "old scenes must retain no added emission");
    std::cout << "Lighting settings serialization and uniform layout tests passed.\n";
}
