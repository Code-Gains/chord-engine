#pragma once

#include "System.h"

namespace Engine {
class Core;
}

class VelocityIntegrationSystem : public System {
public:
    VelocityIntegrationSystem(entt::registry& registry, Engine::Core* core = nullptr);

    void Update(float deltaTime) override {};
    void FixedUpdate(float deltaTime) override;
    void DrawUi() override {};
    void Draw() override {};

private:
    Engine::Core* _core = nullptr;
};
