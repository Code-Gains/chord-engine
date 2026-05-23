#include "EntityViewer.h"

void EntityViewer::Update(float deltaTime)
{
}

void EntityViewer::DrawUi()
{
     ImGui::Begin("Entity Viewer");

    // auto view = _registry.view<Transform>();

    // for (auto entity : view)
    // {
    //     std::string label = "Entity " + std::to_string((int)entt::to_integral(entity));

    //     if (ImGui::Selectable(label.c_str(), _selectedEntity == entity))
    //     {
    //         _selectedEntity = entity;
    //     }
    // }
    auto& selectedEntity = _registryViewerPtr->GetSelectedEntity();

    if (selectedEntity != entt::null && _registry.valid(selectedEntity))
    {
        for (auto& ui : _componentUis)
        {
            ui->Draw(_registry, selectedEntity);
        }
    }

    ImGui::End();
}

EntityViewer::EntityViewer(entt::registry &registry, RegistryViewer* registryViewerPtr) : System(registry), _registryViewerPtr(registryViewerPtr)
{
    _componentUis.push_back(std::make_unique<NameComponentUi>());
    _componentUis.push_back(std::make_unique<TransformComponentUi>());
    _componentUis.push_back(std::make_unique<SunlightComponentUI>());
}
