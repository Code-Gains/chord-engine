#include "InputSystem.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
    void UpdateDigitalState(KeyState& state, bool currentlyPressed)
    {
        state.pressed = currentlyPressed && !state.held;
        state.released = !currentlyPressed && state.held;
        state.held = currentlyPressed;
    }

    float ApplyAxisDeadzone(float value, float deadzone)
    {
        const float magnitude = std::abs(value);
        if (magnitude <= deadzone) {
            return 0.0f;
        }

        const float normalized = (magnitude - deadzone) / (1.0f - deadzone);
        return std::copysign(std::clamp(normalized, 0.0f, 1.0f), value);
    }

    float NormalizeTrigger(float value)
    {
        return std::clamp((value + 1.0f) * 0.5f, 0.0f, 1.0f);
    }
}

InputSystem::InputSystem(entt::registry& registry, entt::entity inputEntity, Engine::WindowGLFW* engineWindow) : System(registry), _inputEntity(inputEntity), _engineWindow(engineWindow) {
    _window = _engineWindow->GetNativeHandle();
    _monitoredKeys = {
        GLFW_KEY_W,
        GLFW_KEY_A,
        GLFW_KEY_S,
        GLFW_KEY_D,
        GLFW_KEY_SPACE,
        GLFW_KEY_LEFT_ALT,
        GLFW_KEY_RIGHT_ALT,
        GLFW_KEY_ENTER,
        GLFW_KEY_LEFT_SHIFT,
        GLFW_KEY_F,
        GLFW_KEY_ESCAPE
    };
     _monitoredMouseButtons = {
        GLFW_MOUSE_BUTTON_LEFT,
        GLFW_MOUSE_BUTTON_RIGHT,
        GLFW_MOUSE_BUTTON_MIDDLE
    };

    _engineWindow->SetScrollCallback([this](double xOffset, double yOffset) {
        OnScroll(xOffset, yOffset);
    });
}

void InputSystem::Update(float deltaTime)
{
    auto & inputState = _registry.get<InputState>(_inputEntity);

    double x, y;
    glfwGetCursorPos(_window, &x, &y);

    inputState.deltaX = x - inputState.mouseX;
    inputState.deltaY = y - inputState.mouseY;

    inputState.mouseX = x;
    inputState.mouseY = y;
    inputState.scrollX = _pendingScrollX;
    inputState.scrollY = _pendingScrollY;
    _pendingScrollX = 0.0;
    _pendingScrollY = 0.0;

    for (int key : _monitoredKeys) {
        KeyState& state = inputState.keys[key];
        UpdateDigitalState(state, glfwGetKey(_window, key) == GLFW_PRESS);
    }

    // Update mouse button states
    for (int button : _monitoredMouseButtons) {
        KeyState& state = inputState.mouseButtons[button];
        UpdateDigitalState(state, glfwGetMouseButton(_window, button) == GLFW_PRESS);
    }

    UpdateGamepad(inputState);

    bool altPressed = inputState.keys[GLFW_KEY_LEFT_ALT].held ||
                      inputState.keys[GLFW_KEY_RIGHT_ALT].held;
    if (altPressed && inputState.keys[GLFW_KEY_ENTER].pressed) {
        if (_engineWindow) {
            _engineWindow->ToggleMaximize();
        }
    }
}

void InputSystem::OnScroll(double xOffset, double yOffset)
{
    _pendingScrollX += xOffset;
    _pendingScrollY += yOffset;
}

void InputSystem::UpdateGamepad(InputState& inputState)
{
    int activeGamepad = -1;
    for (int joystick = GLFW_JOYSTICK_1; joystick <= GLFW_JOYSTICK_LAST; ++joystick) {
        if (glfwJoystickIsGamepad(joystick)) {
            activeGamepad = joystick;
            break;
        }
    }

    inputState.activeGamepad = activeGamepad;
    inputState.activeGamepadName.clear();
    inputState.gamepadAxes.assign(GLFW_GAMEPAD_AXIS_LAST + 1, 0.0f);
    inputState.leftStick = glm::vec2{ 0.0f };
    inputState.rightStick = glm::vec2{ 0.0f };
    inputState.leftTrigger = 0.0f;
    inputState.rightTrigger = 0.0f;

    GLFWgamepadstate gamepadState{};
    if (activeGamepad != -1 && glfwGetGamepadState(activeGamepad, &gamepadState)) {
        const char* gamepadName = glfwGetGamepadName(activeGamepad);
        if (gamepadName) {
            inputState.activeGamepadName = gamepadName;
        }

        for (int button = 0; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
            UpdateDigitalState(
                inputState.gamepadButtons[button],
                gamepadState.buttons[button] == GLFW_PRESS);
        }

        for (int axis = 0; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
            inputState.gamepadAxes[axis] = gamepadState.axes[axis];
        }

        constexpr float stickDeadzone = 0.20f;
        inputState.leftStick = glm::vec2{
            ApplyAxisDeadzone(gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_X], stickDeadzone),
            ApplyAxisDeadzone(gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_Y], stickDeadzone)
        };
        inputState.rightStick = glm::vec2{
            ApplyAxisDeadzone(gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_X], stickDeadzone),
            ApplyAxisDeadzone(gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y], stickDeadzone)
        };
        inputState.leftTrigger = NormalizeTrigger(gamepadState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
        inputState.rightTrigger = NormalizeTrigger(gamepadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
        return;
    }

    for (auto& [button, state] : inputState.gamepadButtons) {
        UpdateDigitalState(state, false);
    }
}

// void InputSystem::Update(InputState &input, GLFWwindow *window)
// {
//     for (int key : _monitoredKeys) {
//         KeyState& state = input.keys[key];

//         bool currentlyPressed = glfwGetKey(window, key) == GLFW_PRESS;

//         state.pressed  = currentlyPressed && !state.held;
//         state.released = !currentlyPressed && state.held;
//         state.held     = currentlyPressed;
//     }
// }
