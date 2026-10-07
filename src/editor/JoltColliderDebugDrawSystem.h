#pragma once

#include "System.h"

class JoltColliderDebugDrawSystem : public System {
public:
    explicit JoltColliderDebugDrawSystem(entt::registry& registry);

    void Update(float deltaTime) override;
    void DrawUi() override;

private:
    bool _showColliders = false;
    bool _drawOnlyStatic = false;
};
