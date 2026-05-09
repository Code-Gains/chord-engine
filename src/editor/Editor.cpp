#include "engine/Core.h"
#include "engine/Log.h"
#include "engine/VulkanDevice.h"
#include "engine/Renderer.h"
#include "EditorUiManager.h"
#include "RegistryViewer.h"
#include "EntityViewer.h"
#include "NameComponent.h"

int main() {
    ENGINE_LOG_INFO("Editor starting.");
    Engine::Core core;
    core.Init();
    auto& registry = core.GetRegistry();
    core._systems.push_back(std::make_unique<EditorUiManager>(registry));

    auto registryViewer = std::make_unique<RegistryViewer>(registry);
    auto* registryViewerPtr = registryViewer.get();
    core._systems.push_back(std::move(registryViewer));

    core._systems.push_back(std::make_unique<EntityViewer>(registry, registryViewerPtr));

    auto meshEntity = registry.create();
    registry.emplace<MeshComponent>(meshEntity, core._testMeshes[0]);
    auto transform = Transform();
    transform.position = glm::vec3 {0.0f, 0.0f, 0.0f};
    //transform.scale = glm::vec3 {10.0f, 10.0f, 10.0f};
    registry.emplace<Transform>(meshEntity, transform);
    registry.emplace<SingleRenderTag>(meshEntity);
    auto nameComponent = NameComponent("Cube");
    registry.emplace<NameComponent>(meshEntity, nameComponent);

    core.Run();

    ENGINE_LOG_INFO("Editor finished.");
    return 0;
}
