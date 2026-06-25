#pragma once

#include <string>
#include <entt/entt.hpp>
#include "System.h"
//#include "ECS.hpp"

/* Debugger class for Core ECS that provides a UI
   to visually diagnose problems, should be completely disabled in release config
*/
class EcsDebugger : public System {
    //ECS* _ecs;
    bool _enabled = true; // TODO SET FALSE
    float _totalDeltaTime = 0;
    float _fps = 0;
    uint32_t _framesPassed = 0;
    float _averageFramerate = 0;
    float frameTimeMs = 0.0f;


public:
    EcsDebugger(entt::registry& registry);
    //ECSDebugger(ECS* ecs);
    ~EcsDebugger();

    void Update(float deltaTime) override;
    virtual void DrawUi() override;

    void Toggle();

};
