#pragma once
#include "System.h"
#include <entt/entt.hpp>

namespace Engine {
class Core;
}

class CameraSystem : public System {
public:
    CameraSystem(entt::registry& registry, Engine::Core* core = nullptr);
    void Update(float deltaTime) override;

private:
    Engine::Core* _core = nullptr;
};
