#pragma once
#include <engine/System.h>
#include <entt/entt.hpp>

class EditorUiManager : public System {
    void Update(float deltaTime) override;
    void FixedUpdate(float deltaTime) override {};
    virtual void DrawUi() override;
    virtual void Draw() override {};


    bool _ecsDebuggerEnabled = true;

public:
    EditorUiManager(entt::registry& registry);
    void ToggleEcsDebugger();
};