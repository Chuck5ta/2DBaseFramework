#pragma once

#include "gfx/VkCommon.h"

#include <SDL3/SDL_video.h>
#include <glm/vec2.hpp>

#include <string>
#include <vector>

namespace core {

struct WindowDesc {
    std::string title = "Game";
    int width = 1280;
    int height = 720;
    bool resizable = true;
    bool fullscreen = false;
};

// Owns the SDL window. The swapchain is sized from pixelSize(), which already
// accounts for high-DPI displays.
class Window {
public:
    explicit Window(const WindowDesc& desc);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SDL_Window* handle() const { return window_; }

    glm::ivec2 pixelSize() const;      // framebuffer size in pixels
    float pixelDensity() const;        // pixels per window coordinate
    bool isMinimized() const;
    bool isFullscreen() const;

    void setTitle(const std::string& title);
    void setFullscreen(bool fullscreen);
    void toggleFullscreen() { setFullscreen(!isFullscreen()); }

    std::vector<const char*> requiredVulkanExtensions() const;
    VkSurfaceKHR createSurface(VkInstance instance) const;

private:
    SDL_Window* window_ = nullptr;
};

} // namespace core
