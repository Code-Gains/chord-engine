#pragma once

#include "System.h"

namespace Engine {
class Core;
}

class GravitySimulationSystem : public System {
public:
    GravitySimulationSystem(entt::registry& registry, Engine::Core* core = nullptr);

    void Update(float deltaTime) override {};
    void FixedUpdate(float deltaTime) override;
    void DrawUi() override {};
    void Draw() override {};

    float gravitationalConstant = 1.0f;
    float minDistanceSquared = 0.001f;

private:
    Engine::Core* _core = nullptr;
};
