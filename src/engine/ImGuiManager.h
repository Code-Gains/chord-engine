#pragma once
#include "System.h"
#include <entt/entt.hpp>

class ImGuiManager : public System {
    void Update(float deltaTime) override {};
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};

public:
    ImGuiManager(entt::registry& registry);
    void ToggleEcsDebugger();
};