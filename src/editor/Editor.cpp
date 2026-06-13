#include "engine/Core.h"
#include "engine/Log.h"
#include "engine/VulkanDevice.h"
#include "engine/Renderer.h"
#include "engine/ImGuiManager.h"
#include "Camera.h"
#include "RegistryViewer.h"
#include "EntityViewer.h"
#include "AssetViewer.h"
#include "NameComponent.h"
#include "SunlightComponent.h"

int main() {
    ENGINE_LOG_INFO("Editor starting.");
    Engine::Core core;
    core.SetEngineRoot("../../..");
    core.SetProjectRoot("../../..");
    core.Init();

    std::vector<std::shared_ptr<MeshAsset>> meshes;
    meshes = core.LoadGltfMeshes(&core, "assets/DamagedHelmet.gltf").value();
    auto& registry = core.GetRegistry();

    auto editorCameraEntity = registry.create();
    auto& editorCameraTransform = registry.emplace<Transform>(editorCameraEntity);
    editorCameraTransform.position = { 0.0f, 0.0f, 10.0f };
    registry.emplace<Camera>(editorCameraEntity);
    registry.emplace<ActiveCameraTag>(editorCameraEntity);
    registry.emplace<Engine::CoreOwnedTag>(editorCameraEntity);
    registry.emplace<NameComponent>(editorCameraEntity, "Editor Camera");

    core._systems.push_back(std::make_unique<ImGuiManager>(registry, &core));

    auto registryViewer = std::make_unique<RegistryViewer>(registry, &core);
    auto* registryViewerPtr = registryViewer.get();
    core._systems.push_back(std::move(registryViewer));

    core._systems.push_back(std::make_unique<EntityViewer>(registry, registryViewerPtr));
    core._systems.push_back(std::make_unique<AssetViewer>(registry, &core, registryViewerPtr));

    auto meshEntity = registry.create();
    registry.emplace<MeshComponent>(meshEntity, meshes[0]);
    auto transform = Transform();
    transform.position = glm::vec3 {0.0f, 0.0f, 0.0f};
    //transform.scale = glm::vec3 {10.0f, 10.0f, 10.0f};
    registry.emplace<Transform>(meshEntity, transform);
    registry.emplace<SingleRenderTag>(meshEntity);
    auto nameComponent = NameComponent("Helmet");
    registry.emplace<NameComponent>(meshEntity, nameComponent);

    // Sunlight
    auto sunEntity = registry.create();
    registry.emplace<Transform>(sunEntity);
    registry.emplace<SunlightComponent>(sunEntity);
    auto sunNameComponent = NameComponent("Sun");
    registry.emplace<NameComponent>(sunEntity, sunNameComponent);


    core.Run();

    ENGINE_LOG_INFO("Editor finished.");
    return 0;
}
