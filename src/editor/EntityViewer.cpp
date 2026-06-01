#include "EntityViewer.h"
#include <ImGuiWindowRegistry.h>

void EntityViewer::Update(float deltaTime)
{
}

void EntityViewer::DrawUi()
{
    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    if (!windowRegistry.IsWindowOpen("Entity Viewer"))
        return;

    bool open = true;

    if (ImGui::Begin("Entity Viewer", &open))
    {
        auto& selectedEntity = _registryViewerPtr->GetSelectedEntity();

        if (selectedEntity != entt::null && _registry.valid(selectedEntity))
        {
            for (auto& ui : _componentUis)
            {
                ui->Draw(_registry, selectedEntity);
            }
        }
    }

    ImGui::End();

    windowRegistry.SetWindowOpen("Entity Viewer", open);
}

EntityViewer::EntityViewer(entt::registry &registry, RegistryViewer* registryViewerPtr) : System(registry), _registryViewerPtr(registryViewerPtr)
{
    _componentUis.push_back(std::make_unique<NameComponentUi>());
    _componentUis.push_back(std::make_unique<TransformComponentUi>());
    _componentUis.push_back(std::make_unique<SunlightComponentUI>());

    auto& windowRegistry = _registry.ctx().get<ImGuiWindowRegistry>();

    windowRegistry.RegisterWindow(
        "Entity Viewer",
        true
    );
}
