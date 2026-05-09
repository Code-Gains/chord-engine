#pragma once
#include <engine/System.h>
#include "RegistryViewer.h"
#include "ViewerComponentUi.h"

class EntityViewer : public System {
    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

    RegistryViewer* _registryViewerPtr;
    std::vector<std::unique_ptr<ViewerComponentUi>> _componentUis;

public:
    EntityViewer(entt::registry& registry, RegistryViewer* registryViewerPtr);
};