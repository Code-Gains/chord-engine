#pragma once
#include <engine/System.h>
#include "RegistryViewer.h"
#include "ViewerComponentUi.h"

#include <functional>
#include <string>

class EntityViewer : public System {
    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

    RegistryViewer* _registryViewerPtr;
    std::vector<std::unique_ptr<ViewerComponentUi>> _componentUis;
    struct ComponentMenuEntry {
        std::string label;
        std::function<bool(entt::registry&, entt::entity)> canAdd;
        std::function<void(entt::registry&, entt::entity)> add;
    };

    std::vector<ComponentMenuEntry> _componentMenuEntries;

public:
    EntityViewer(entt::registry& registry, RegistryViewer* registryViewerPtr);
    void AddComponentUi(std::unique_ptr<ViewerComponentUi> componentUi);
    void AddComponentMenuItem(
        std::string label,
        std::function<bool(entt::registry&, entt::entity)> canAdd,
        std::function<void(entt::registry&, entt::entity)> add
    );
};
