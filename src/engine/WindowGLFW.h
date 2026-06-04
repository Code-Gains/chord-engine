#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>
#include <functional>
#include <vector>

namespace Engine {
    class WindowGLFW {
        GLFWwindow* _window = nullptr;

        uint32_t _width;
        uint32_t _height;
        int _windowedX;
        int _windowedY;
        int _windowedWidth;
        int _windowedHeight;
        bool _resized = false;
        bool _isFullscreen = false;
        std::function<void(double, double)> _scrollCallback;
        void InitGLFW();

    public:
        // Control
        WindowGLFW(uint32_t width, uint32_t height, const char* title);
        ~WindowGLFW();
        void ShutdownGLFW();

        // non-copyable
        WindowGLFW(const WindowGLFW&) = delete;
        WindowGLFW& operator=(const WindowGLFW&) = delete;

        void PollEvents();
        bool ShouldClose() const;

        GLFWwindow* GetNativeHandle() const { return _window; };
        VkSurfaceKHR CreateAndGetWindowSurface(VkInstance instance);

        // Vulkan
        std::vector<const char*> GetRequiredVulkanExtensions() const;

        // Getters
        uint32_t GetWidth();
        uint32_t GetHeight();

        bool WasResized();
        void ResetResizedFlag();
        void ToggleMaximize();
        void SetScrollCallback(std::function<void(double, double)> callback);
        void HandleScroll(double xOffset, double yOffset);
    };
} // namespace engine
