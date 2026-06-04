#include "InputSystem.h"
#include <iostream>


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
        GLFW_KEY_SPACE,
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

        bool currentlyPressed = glfwGetKey(_window, key) == GLFW_PRESS;

        state.pressed  = currentlyPressed && !state.held;
        state.released = !currentlyPressed && state.held;
        state.held     = currentlyPressed;
    }

    // Update mouse button states
    for (int button : _monitoredMouseButtons) {
        KeyState& state = inputState.mouseButtons[button];

        bool currentlyPressed = glfwGetMouseButton(_window, button) == GLFW_PRESS;

        state.pressed  = currentlyPressed && !state.held;
        state.released = !currentlyPressed && state.held;
        state.held     = currentlyPressed;
    }

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
