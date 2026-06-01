#pragma once
#include <engine/System.h>
#include <entt/entt.hpp>
#include "ViewerComponentUi.h"

class RegistryViewer : public System {
    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

    entt::entity _selectedEntity{ entt::null };


public:
    RegistryViewer(entt::registry& registry);
    const entt::entity& GetSelectedEntity() const;
};