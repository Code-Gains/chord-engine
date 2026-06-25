#pragma once
#include <entt/entt.hpp>

class System {
public:
	System(entt::registry& registry) : _registry(registry) {}
	virtual ~System() = default;
	virtual void OnPlayStart() {}
	virtual void OnPlayStop() {}
	virtual void Update(float deltaTime) {}
	virtual void FixedUpdate(float deltaTime) {}
	virtual void DrawUi() {}
	virtual void Draw() {}

protected:
	entt::registry& _registry;
};
