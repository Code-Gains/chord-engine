#pragma once

#include "System.h"

namespace Engine {
class Core;
}

class VelocityIntegrationSystem : public System {
public:
    VelocityIntegrationSystem(entt::registry& registry, Engine::Core* core = nullptr);

    void FixedUpdate(float deltaTime) override;

private:
    Engine::Core* _core = nullptr;
};
