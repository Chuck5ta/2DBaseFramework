#include "core/Window.h"

#include "core/Log.h"

#include <SDL3/SDL_vulkan.h>

namespace core {

Window::Window(const WindowDesc& desc)
{
    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable)  flags |= SDL_WINDOW_RESIZABLE;
    if (desc.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    window_ = SDL_CreateWindow(desc.title.c_str(), desc.width, desc.height, flags);
    if (!window_)
        fatal("Failed to create window: %s", SDL_GetError());
}

Window::~Window()
{
    if (window_)
        SDL_DestroyWindow(window_);
}

glm::ivec2 Window::pixelSize() const
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    return {w, h};
}

float Window::pixelDensity() const
{
    const float d = SDL_GetWindowPixelDensity(window_);
    return d > 0.0f ? d : 1.0f;
}

bool Window::isMinimized() const
{
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED)
        return true;
    const glm::ivec2 size = pixelSize();
    return size.x == 0 || size.y == 0;
}

bool Window::isFullscreen() const
{
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
}

void Window::setTitle(const std::string& title)
{
    SDL_SetWindowTitle(window_, title.c_str());
}

void Window::setFullscreen(bool fullscreen)
{
    // Uses borderless "desktop" fullscreen, which is what you want on
    // Windows/Linux desktops and is what gamescope (Steam Deck) expects.
    SDL_SetWindowFullscreen(window_, fullscreen);
}

std::vector<const char*> Window::requiredVulkanExtensions() const
{
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names)
        fatal("SDL could not query Vulkan instance extensions: %s", SDL_GetError());
    return std::vector<const char*>(names, names + count);
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface))
        fatal("Failed to create Vulkan surface: %s", SDL_GetError());
    return surface;
}

} // namespace core
