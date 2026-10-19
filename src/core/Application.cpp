#include "core/Application.h"

#include "core/Log.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace core {

namespace {

// Reads an integer environment variable; returns `fallback` if unset.
long envInt(const char* name, long fallback)
{
    const char* v = SDL_getenv(name);
    return (v && *v) ? std::strtol(v, nullptr, 10) : fallback;
}

} // namespace

Application::Application(const AppConfig& config)
    : config_(config)
{
    SDL_SetAppMetadata(config.window.title.c_str(), "0.1.0", nullptr);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        fatal("SDL_Init failed: %s", SDL_GetError());

#ifndef NDEBUG
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_DEBUG);
#endif

    config_.validation = envInt("GAME_VALIDATION", config_.validation ? 1 : 0) != 0;

    window_ = std::make_unique<Window>(config_.window);
    input_ = std::make_unique<Input>();
    input_->setPixelDensity(window_->pixelDensity());

    gfx::ContextDesc ctxDesc;
    ctxDesc.appName = config_.window.title.c_str();
    ctxDesc.validation = config_.validation;
    context_ = std::make_unique<gfx::VulkanContext>(*window_, ctxDesc);
    renderer_ = std::make_unique<gfx::Renderer>(*context_, *window_, config_.renderer);
}

Application::~Application()
{
    // Order matters: GPU objects before the device, the device before the window.
    renderer_.reset();
    context_.reset();
    input_.reset();
    window_.reset();
    SDL_Quit();
}

void Application::pumpEvents()
{
    input_->beginFrame();
    input_->setPixelDensity(window_->pixelDensity());

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
            // Anything else that invalidates the swapchain (restore from
            // minimise, display changes) is reported by acquire/present.
            const VkExtent2D current = renderer_->extent();
            if (uint32_t(e.window.data1) != current.width || uint32_t(e.window.data2) != current.height)
                renderer_->requestResize();
            break;
        }
        default:
            break;
        }
        input_->handleEvent(e);
    }

    if (config_.f11TogglesFullscreen) {
        const bool altEnter = input_->keyPressed(SDL_SCANCODE_RETURN) &&
                              (input_->keyDown(SDL_SCANCODE_LALT) || input_->keyDown(SDL_SCANCODE_RALT));
        if (input_->keyPressed(SDL_SCANCODE_F11) || altEnter)
            window_->toggleFullscreen();
    }
}

int Application::run(Game& game)
{
    game.onInit(*this);

    // Testing aid: GAME_MAX_FRAMES=N exits after N rendered frames.
    const long maxFrames = envInt("GAME_MAX_FRAMES", 0);
    long renderedFrames = 0;

    const double frequency = double(SDL_GetPerformanceFrequency());
    const double fixedStep = config_.fixedTimestep;
    uint64_t last = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    double fpsTimer = 0.0;
    int fpsFrames = 0;
    VkExtent2D lastExtent = renderer_->extent();

    running_ = true;
    while (running_) {
        pumpEvents();
        if (!running_)
            break;

        if (window_->isMinimized()) {
            SDL_Delay(16);
            last = SDL_GetPerformanceCounter(); // don't accumulate time while hidden
            continue;
        }

        const uint64_t now = SDL_GetPerformanceCounter();
        // Clamp so a breakpoint or hitch doesn't trigger a huge catch-up.
        const double dt = std::min(double(now - last) / frequency, 0.25);
        last = now;
        time_ += dt;

        // --- Simulation ---
        accumulator += dt;
        while (accumulator >= fixedStep) {
            game.onFixedUpdate(*this, float(fixedStep));
            accumulator -= fixedStep;
        }
        alpha_ = float(accumulator / fixedStep);
        game.onUpdate(*this, float(dt));
        if (!running_)
            break;

        // --- Rendering ---
        if (gfx::FrameContext* frame = renderer_->beginFrame()) {
            if (frame->extent.width != lastExtent.width || frame->extent.height != lastExtent.height) {
                lastExtent = frame->extent;
                game.onResize(*this, lastExtent.width, lastExtent.height);
            }
            game.onPreRender(*this, *frame);
            renderer_->beginMainPass();
            game.onRender(*this, *frame);
            renderer_->endMainPass();
            renderer_->endFrame();

            if (maxFrames > 0 && ++renderedFrames >= maxFrames)
                running_ = false;
        }

        // --- FPS counter ---
        ++fpsFrames;
        fpsTimer += dt;
        if (fpsTimer >= 0.5) {
            fps_ = float(fpsFrames / fpsTimer);
            fpsFrames = 0;
            fpsTimer = 0.0;
            if (config_.showFpsInTitle) {
                char title[256];
                SDL_snprintf(title, sizeof(title), "%s  |  %.0f fps  %.2f ms", config_.window.title.c_str(),
                             double(fps_), fps_ > 0.0f ? 1000.0 / double(fps_) : 0.0);
                window_->setTitle(title);
            }
        }
    }

    renderer_->waitIdle();
    game.onShutdown(*this);
    return 0;
}

} // namespace core
