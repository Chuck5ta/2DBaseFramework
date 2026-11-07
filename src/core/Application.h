#pragma once

#include "audio/Audio.h"
#include "core/Actions.h"
#include "core/GameState.h"
#include "core/SaveData.h"
#include "core/Input.h"
#include "debugui/DebugUI.h"
#include "core/Window.h"
#include "gfx/Renderer.h"
#include "gfx/VulkanContext.h"

#include <array>
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

    // A game state that pauses the layers below it (a pause menu, a title screen) was
    // pushed, so onFixedUpdate/onUpdate stop; onResume when it is gone. Good places to
    // pause sounds, or to snap interpolated positions so a frozen frame doesn't shimmer.
    virtual void onPause(Application&) {}
    virtual void onResume(Application&) {}

    // Swapchain size changed (pixels).
    virtual void onResize(Application&, uint32_t /*width*/, uint32_t /*height*/) {}

    // Build your debug windows here with ImGui (see debugui/DebugUI.h). Called
    // once per frame after onUpdate, only while the debug UI is visible.
    virtual void onDebugUI(Application&) {}
};

struct AppConfig {
    WindowDesc window;
    gfx::RendererConfig renderer;
    audio::AudioDesc audio;
    debugui::DebugUIDesc debugUI;
    double fixedTimestep = 1.0 / 60.0;
    // Validation layers: on in debug builds by default. Can also be forced
    // with the environment variable GAME_VALIDATION=1 (or 0).
#ifdef NDEBUG
    bool validation = false;
#else
    bool validation = true;
#endif
    // Where settings and saves go (core/Paths.h): a folder of the player's,
    //   Windows: %APPDATA%\<organization>\<application>    Linux / Steam Deck: ~/.local/share/...
    // Use your studio's (or your own) name and the game's name, and don't change them
    // once players have saves, or the game won't find them.
    std::string organization = "MyStudio";
    std::string application = "MyGame";
    // Remember the player's settings in settings.json: fullscreen, vsync, volumes, mute
    // and changed controls are loaded at start and saved at exit. See app.settings().
    bool rememberSettings = true;
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
    Actions& actions()          { return actions_; } // named actions with rebindable inputs
    StateStack& states()        { return states_; }  // title screens, pause menus... (core/GameState.h)
    gfx::VulkanContext& gpu()   { return *context_; }
    gfx::Renderer& renderer()   { return *renderer_; }
    audio::AudioEngine& audio() { return *audio_; }
    // nullptr when the debug UI is disabled (release builds by default).
    debugui::DebugUI* debugUI() { return debugUI_.get(); }
    const AppConfig& config() const { return config_; }

    // The player's settings (settings.json in the save folder). The framework fills in
    // "video", "audio" and "controls"; add your game's own under "game", e.g.
    //   app.settings().set("game.difficulty", 2);
    SaveData& settings() { return settings_; }
    // Writes the current fullscreen/vsync/volume/mute/controls into settings() and saves
    // the file (also done at exit). Call it when the player leaves an options screen.
    bool saveSettings();
    // Puts fullscreen, vsync, volumes, mute and controls back to the game's defaults.
    void resetSettings();

    double time() const               { return time_; }          // seconds since start
    float fps() const                 { return fps_; }
    float interpolationAlpha() const  { return alpha_; }         // 0..1 between fixed steps

private:
    void pumpEvents();
    void drawEnginePanel();
    void drawAudioEffects(audio::AudioEngine& audio);

    AppConfig config_;
    std::unique_ptr<Window> window_;
    std::unique_ptr<Input> input_;
    std::unique_ptr<gfx::VulkanContext> context_;
    std::unique_ptr<gfx::Renderer> renderer_;
    std::unique_ptr<audio::AudioEngine> audio_;
    std::unique_ptr<debugui::DebugUI> debugUI_;

    bool running_ = false;
    double time_ = 0.0;
    Actions actions_;
    StateStack states_;
    SaveData settings_{"settings.json"};
    AppConfig defaults_; // the config before settings.json changed it
    float fps_ = 0.0f;
    float alpha_ = 0.0f;

    // Engine panel state: recent frame times (ms) for the graph.
    std::array<float, 180> frameTimes_{};
    size_t frameTimeIndex_ = 0;
    bool showImGuiDemo_ = false;
};

} // namespace core
