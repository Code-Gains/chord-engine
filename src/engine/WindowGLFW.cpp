#include "WindowGLFW.h"
#include <stdexcept>
#include <iostream>
#include <GLFW/glfw3.h>
#include <algorithm>

#include "Log.h"

namespace Engine {
    namespace {
        int OverlapArea(
            int firstX,
            int firstY,
            int firstWidth,
            int firstHeight,
            int secondX,
            int secondY,
            int secondWidth,
            int secondHeight)
        {
            const int left = std::max(firstX, secondX);
            const int right = std::min(firstX + firstWidth, secondX + secondWidth);
            const int top = std::max(firstY, secondY);
            const int bottom = std::min(firstY + firstHeight, secondY + secondHeight);

            if (right <= left || bottom <= top) {
                return 0;
            }

            return (right - left) * (bottom - top);
        }
    }

    WindowGLFW::WindowGLFW(uint32_t width, uint32_t height, const char* title) {
        _width = width;
        _height = height;
        InitGLFW();

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        _window = glfwCreateWindow(_width, _height, title, nullptr, nullptr);
        if (!_window)
            throw std::runtime_error("Failed to create GLFW window");
        
        // hook for our window to track internal GLFW window size info
        glfwSetFramebufferSizeCallback(_window, [](GLFWwindow* window, int width, int height) {
            // Store the new size in the user pointer
            auto win = reinterpret_cast<Engine::WindowGLFW*>(glfwGetWindowUserPointer(window));
            if (win) {
                win->_width = static_cast<int>(width);
                win->_height = static_cast<int>(height);
                win->_resized = true; // flag for Core to handle
            }
        });
        glfwSetWindowUserPointer(_window, this);
        glfwSetScrollCallback(_window, [](GLFWwindow* window, double xOffset, double yOffset) {
            auto win = reinterpret_cast<Engine::WindowGLFW*>(glfwGetWindowUserPointer(window));
            if (win) {
                win->HandleScroll(xOffset, yOffset);
            }
        });
        //glfwSetInputMode(_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        SaveWindowedBounds();
        SetBorderlessFullscreen(true);
        ENGINE_LOG_INFO("Window created");
    }

    WindowGLFW::~WindowGLFW() {
        ShutdownGLFW();
    }

    void WindowGLFW::InitGLFW() {
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW");
    }

    void WindowGLFW::ShutdownGLFW() {
        if (_window) {
            glfwDestroyWindow(_window);
            _window = nullptr;
        }
        glfwTerminate();
    }

    void WindowGLFW::PollEvents() {
        glfwPollEvents();
    }

    void WindowGLFW::Show()
    {
        if (_window) {
            glfwShowWindow(_window);
        }
    }

    bool WindowGLFW::ShouldClose() const {
        return glfwWindowShouldClose(_window);
    }

    VkSurfaceKHR WindowGLFW::CreateAndGetWindowSurface(VkInstance instance) {
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(instance, _window, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan surface");
        }
        return surface;
    }

    std::vector<const char*> WindowGLFW::GetRequiredVulkanExtensions() const {
        uint32_t count = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&count);
        return std::vector<const char*>(extensions, extensions + count);
    }

    uint32_t Engine::WindowGLFW::GetWidth() {
        return _width;
    }

    uint32_t Engine::WindowGLFW::GetHeight() {
        return _height;
    }

    bool Engine::WindowGLFW::WasResized() {
        return _resized;
    }
    void Engine::WindowGLFW::ResetResizedFlag() {
        _resized = false;
    }

    GLFWmonitor* WindowGLFW::GetBestMonitor() const
    {
        int windowX = 0;
        int windowY = 0;
        int windowWidth = static_cast<int>(_width);
        int windowHeight = static_cast<int>(_height);

        if (_window) {
            glfwGetWindowPos(_window, &windowX, &windowY);
            glfwGetWindowSize(_window, &windowWidth, &windowHeight);
        }

        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        GLFWmonitor* bestMonitor = glfwGetPrimaryMonitor();
        int bestOverlap = 0;

        for (int i = 0; i < monitorCount; ++i) {
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitors[i], &monitorX, &monitorY);

            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (!mode) {
                continue;
            }

            const int overlap = OverlapArea(
                windowX,
                windowY,
                windowWidth,
                windowHeight,
                monitorX,
                monitorY,
                mode->width,
                mode->height);

            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                bestMonitor = monitors[i];
            }
        }

        return bestMonitor;
    }

    void WindowGLFW::SaveWindowedBounds()
    {
        if (!_window || _isBorderlessFullscreen) {
            return;
        }

        glfwGetWindowPos(_window, &_windowedX, &_windowedY);
        glfwGetWindowSize(_window, &_windowedWidth, &_windowedHeight);
    }

    void WindowGLFW::SetBorderlessFullscreen(bool enabled)
    {
        if (!_window || enabled == _isBorderlessFullscreen) {
            return;
        }

        _resized = true;

        if (enabled) {
            if (_isExclusiveFullscreen) {
                SetExclusiveFullscreen(false);
            }

            SaveWindowedBounds();

            GLFWmonitor* monitor = GetBestMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            int monitorX = 0;
            int monitorY = 0;
            glfwGetMonitorPos(monitor, &monitorX, &monitorY);

            glfwSetWindowAttrib(_window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(
                _window,
                nullptr,
                monitorX,
                monitorY,
                mode->width,
                mode->height,
                mode->refreshRate);
            _isBorderlessFullscreen = true;
            return;
        }

        glfwSetWindowMonitor(
            _window,
            nullptr,
            _windowedX,
            _windowedY,
            _windowedWidth,
            _windowedHeight,
            0);
        glfwSetWindowAttrib(_window, GLFW_DECORATED, GLFW_TRUE);
        _isBorderlessFullscreen = false;
    }

    void WindowGLFW::SetExclusiveFullscreen(bool enabled)
    {
        if (!_window || enabled == _isExclusiveFullscreen) {
            return;
        }

        _resized = true;

        if (enabled) {
            if (_isBorderlessFullscreen) {
                SetBorderlessFullscreen(false);
            }
            else {
                SaveWindowedBounds();
            }

            GLFWmonitor* monitor = GetBestMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowAttrib(_window, GLFW_DECORATED, GLFW_TRUE);
            glfwSetWindowMonitor(
                _window,
                monitor,
                0,
                0,
                mode->width,
                mode->height,
                mode->refreshRate);
            _isExclusiveFullscreen = true;
            return;
        }

        glfwSetWindowMonitor(
            _window,
            nullptr,
            _windowedX,
            _windowedY,
            _windowedWidth,
            _windowedHeight,
            0);
        glfwSetWindowAttrib(_window, GLFW_DECORATED, GLFW_TRUE);
        _isExclusiveFullscreen = false;
    }

    void WindowGLFW::ToggleMaximize()
    {
        SetBorderlessFullscreen(!_isBorderlessFullscreen);
    }

    void WindowGLFW::SetScrollCallback(std::function<void(double, double)> callback)
    {
        _scrollCallback = std::move(callback);
    }

    void WindowGLFW::HandleScroll(double xOffset, double yOffset)
    {
        if (_scrollCallback) {
            _scrollCallback(xOffset, yOffset);
        }
    }
} // namespace engine
