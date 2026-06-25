#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "WindowGLFW.h"
#include "System.h"
#include <glm/glm.hpp>


struct KeyState {
    bool pressed = false;   // this frame
    bool held = false;      // is being held down
    bool released = false;  // released this frame
};

struct InputState {
    std::unordered_map<int, KeyState> keys;
    std::unordered_map<int, KeyState> mouseButtons;
    std::unordered_map<int, KeyState> gamepadButtons;
    std::vector<float> gamepadAxes;
    int activeGamepad = -1;
    std::string activeGamepadName;
    glm::vec2 leftStick{ 0.0f };
    glm::vec2 rightStick{ 0.0f };
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
    double mouseX = 0.0;
    double mouseY = 0.0;

    double deltaX = 0.0;
    double deltaY = 0.0;
    double scrollX = 0.0;
    double scrollY = 0.0;
};

class InputSystem : public System {
public:
    InputSystem(entt::registry& registry, entt::entity inputEntity, Engine::WindowGLFW* engineWindow);
    void Update(float deltaTime) override;
    void AddKeyToMonitor(int key) { _monitoredKeys.push_back(key); }

private:
    void OnScroll(double xOffset, double yOffset);
    void UpdateGamepad(InputState& inputState);

    std::vector<int> _monitoredKeys;
    std::vector<int> _monitoredMouseButtons;
    entt::entity _inputEntity;
    GLFWwindow* _window;
    Engine::WindowGLFW* _engineWindow;
    double _pendingScrollX = 0.0;
    double _pendingScrollY = 0.0;
};
