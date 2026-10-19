#pragma once

#include "core/Input.h"
#include "core/Window.h"
#include "gfx/Renderer.h"
#include "gfx/VulkanContext.h"

#include <memory>

namespace core {

class Application;

// Implement this to make a game. Every hook is optional.
class Game {
public:
    virtual ~Game() = default;

    // GPU is ready: create textures, pipelines, meshes...
    virtual void onInit(Application&) {}
    // Called after the GPU is idle, before the Application is torn down.
    virtual void onShutdown(Application&) {}

    // Runs at a fixed rate (AppConfig::fixedTimestep, default 60 Hz), zero or
    // more times per frame. Put physics / gameplay simulation here so it is
    // deterministic and frame-rate independent.
    virtual void onFixedUpdate(Application&, float /*dt*/) {}
    // Runs once per frame with the real frame time. Input handling, camera,
    // animation. Use Application::interpolationAlpha() to blend fixed-step state.
    virtual void onUpdate(Application&, float /*dt*/) {}

    // Record work that must happen *outside* the main render pass:
    // uploads, compute, shadow maps, off-screen targets.
    virtual void onPreRender(Application&, gfx::FrameContext&) {}
    // Draw into the swapchain (main pass already begun, cleared, viewport set).
    virtual void onRender(Application&, gfx::FrameContext&) {}

    // Swapchain size changed (pixels).
    virtual void onResize(Application&, uint32_t /*width*/, uint32_t /*height*/) {}
};

struct AppConfig {
    WindowDesc window;
    gfx::RendererConfig renderer;
    double fixedTimestep = 1.0 / 60.0;
    // Validation layers: on in debug builds by default. Can also be forced
    // with the environment variable GAME_VALIDATION=1 (or 0).
#ifdef NDEBUG
    bool validation = false;
#else
    bool validation = true;
#endif
    bool showFpsInTitle = true;
    bool f11TogglesFullscreen = true; // also Alt+Enter
};

// Owns the platform layer and the GPU, and runs the main loop.
class Application {
public:
    explicit Application(const AppConfig& config);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(Game& game);
    void quit() { running_ = false; }

    Window& window()            { return *window_; }
    Input& input()              { return *input_; }
    gfx::VulkanContext& gpu()   { return *context_; }
    gfx::Renderer& renderer()   { return *renderer_; }
    const AppConfig& config() const { return config_; }

    double time() const               { return time_; }          // seconds since start
    float fps() const                 { return fps_; }
    float interpolationAlpha() const  { return alpha_; }         // 0..1 between fixed steps

private:
    void pumpEvents();

    AppConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<gfx::VulkanContext> context_;
    std::unique_ptr<gfx::Renderer> renderer_;

    bool running_ = false;
    double time_ = 0.0;
    float fps_ = 0.0f;
    float alpha_ = 0.0f;
};

} // namespace core
