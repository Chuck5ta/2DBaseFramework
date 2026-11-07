#include "core/Application.h"

#include "core/Log.h"
#include "core/Paths.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

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

const char* formatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_SRGB:  return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_SRGB:  return "R8G8B8A8_SRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_D32_SFLOAT:     return "D32_SFLOAT";
    case VK_FORMAT_D24_UNORM_S8_UINT: return "D24_UNORM_S8_UINT";
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8_UINT";
    case VK_FORMAT_UNDEFINED:      return "none";
    default:                       return "other";
    }
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

    // The player's settings, applied before the window, renderer and audio are made, so
    // the game starts as they left it (no flash of windowed mode before fullscreen).
    defaults_ = config_;
    setSaveFolderNames(config_.organization, config_.application);
    if (config_.rememberSettings && settings_.load()) {
        const auto volume = [&](const char* key, float fallback) {
            return std::clamp(settings_.getFloat(key, fallback), 0.0f, 2.0f);
        };
        config_.window.fullscreen = settings_.getBool("video.fullscreen", config_.window.fullscreen);
        config_.renderer.vsync = settings_.getBool("video.vsync", config_.renderer.vsync);
        config_.audio.masterVolume = volume("audio.master", config_.audio.masterVolume);
        config_.audio.sfxVolume = volume("audio.sfx", config_.audio.sfxVolume);
        config_.audio.musicVolume = volume("audio.music", config_.audio.musicVolume);
        LOG_INFO("Settings: %s", settings_.path().c_str());
    }

    window_ = std::make_unique<Window>(config_.window);
    input_ = std::make_unique<Input>();
    input_->setPixelDensity(window_->pixelDensity());

    gfx::ContextDesc ctxDesc;
    ctxDesc.appName = config_.window.title.c_str();
    ctxDesc.validation = config_.validation;
    context_ = std::make_unique<gfx::VulkanContext>(*window_, ctxDesc);
    renderer_ = std::make_unique<gfx::Renderer>(*context_, *window_, config_.renderer);
    audio_ = std::make_unique<audio::AudioEngine>(config_.audio);
    if (config_.rememberSettings)
        audio_->setMuted(settings_.getBool("audio.muted", false));
    if (config_.debugUI.enabled)
        debugUI_ = std::make_unique<debugui::DebugUI>(*window_, *context_, *renderer_, config_.debugUI);
}

Application::~Application()
{
    // Order matters: GPU objects before the device, the device before the window.
    debugUI_.reset();
    audio_.reset();
    renderer_.reset();
    context_.reset();
    input_.reset();
    window_.reset();
    SDL_Quit();
}

bool Application::saveSettings()
{
    SaveData& s = settings_;
    s.set("version", 1); // for converting older files if the layout ever changes
    s.set("video.fullscreen", window_->isFullscreen());
    s.set("video.vsync", renderer_->vsync());
    s.set("audio.master", audio_->masterVolume());
    s.set("audio.sfx", audio_->sfxVolume());
    s.set("audio.music", audio_->musicVolume());
    s.set("audio.muted", audio_->muted());

    // Only the actions the player changed: the rest follow the game's defaults, so a
    // default that changes in an update reaches everyone who didn't rebind it.
    nlohmann::json controls = nlohmann::json::object();
    for (const std::string& name : actions_.names()) {
        if (actions_.bindings(name) == actions_.defaultBindings(name))
            continue;
        nlohmann::json list = nlohmann::json::array();
        for (const Binding& b : actions_.bindings(name))
            list.push_back(b.toString());
        controls[name] = std::move(list);
    }
    if (controls.empty())
        s.remove("controls");
    else
        s.json()["controls"] = std::move(controls);

    return s.save();
}

void Application::resetSettings()
{
    window_->setFullscreen(defaults_.window.fullscreen);
    renderer_->setVsync(defaults_.renderer.vsync);
    audio_->setMasterVolume(defaults_.audio.masterVolume);
    audio_->setSfxVolume(defaults_.audio.sfxVolume);
    audio_->setMusicVolume(defaults_.audio.musicVolume);
    audio_->setMuted(false);
    actions_.resetAllToDefaults();
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
        // The debug UI sees events first; it keeps clicks and typing meant for
        // its windows away from the game.
        if (debugUI_ && debugUI_->processEvent(e))
            continue;
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

    // Controls the player changed, now that the game has defined its actions (unknown
    // actions or inputs in the file are skipped).
    if (config_.rememberSettings && settings_.has("controls"))
        actions_.loadBindings(settings_.json()["controls"].dump());

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
        actions_.update(*input_, time_); // named actions from this frame's input
        states_.applyChanges(*this, game); // pushes/pops queued during the last frame

        // --- Simulation ---
        accumulator += dt;
        while (accumulator >= fixedStep) {
            states_.fixedUpdate(*this, game, float(fixedStep)); // the Game, then states (unless paused)
            accumulator -= fixedStep;
        }
        alpha_ = float(accumulator / fixedStep);
        states_.update(*this, game, float(dt));
        audio_->update(float(dt)); // release finished sounds, track motion for Doppler
        if (!running_)
            break;

        // --- Debug UI (only while visible) ---
        if (debugUI_ && debugUI_->beginFrame()) {
            if (config_.debugUI.showEnginePanel)
                drawEnginePanel();
            game.onDebugUI(*this);
            states_.debugUI(*this);
            debugUI_->endFrame();
        }

        // --- Rendering ---
        if (gfx::FrameContext* frame = renderer_->beginFrame()) {
            if (frame->extent.width != lastExtent.width || frame->extent.height != lastExtent.height) {
                lastExtent = frame->extent;
                states_.resize(*this, game, lastExtent.width, lastExtent.height);
            }
            states_.preRender(*this, game, *frame); // the Game, then states (unless hidden)
            renderer_->beginMainPass();
            states_.render(*this, game, *frame);
            if (debugUI_)
                debugUI_->render(frame->cmd); // on top of everything
            renderer_->endMainPass();
            renderer_->endFrame();

            if (maxFrames > 0 && ++renderedFrames >= maxFrames)
                running_ = false;
        }

        // --- FPS counter ---
        frameTimes_[frameTimeIndex_] = float(dt * 1000.0);
        frameTimeIndex_ = (frameTimeIndex_ + 1) % frameTimes_.size();
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
    states_.shutdown(*this); // states may hold GPU objects: the GPU is idle now
    game.onShutdown(*this);
    if (config_.rememberSettings)
        saveSettings(); // after onShutdown, so the game can still set values there

    return 0;
}

// Every audio effect, live. (A game that sets effects itself, e.g. reverb in a
// cave, will change these values as you play; they show what is set now.)
void Application::drawAudioEffects(audio::AudioEngine& a)
{
    if (!ImGui::TreeNode("Audio effects"))
        return;
    constexpr float kGlide = 0.1f; // short glides keep slider drags smooth
    const char* busNames[] = {"Sound effects bus", "Music bus", "Master bus"};
    for (int i = 0; i < 3; ++i) {
        const audio::Bus bus = audio::Bus(i);
        if (!ImGui::TreeNode(busNames[i]))
            continue;
        float lowPass = a.lowPass(bus);
        if (ImGui::SliderFloat("Low-pass", &lowPass, 100.0f, audio::kLowPassOff,
                               lowPass >= audio::kLowPassOff ? "off" : "%.0f Hz", ImGuiSliderFlags_Logarithmic))
            a.setLowPass(bus, lowPass, kGlide);
        float highPass = std::max(a.highPass(bus), 20.0f);
        if (ImGui::SliderFloat("High-pass", &highPass, 20.0f, 5000.0f, highPass <= 20.0f ? "off" : "%.0f Hz",
                               ImGuiSliderFlags_Logarithmic))
            a.setHighPass(bus, highPass <= 20.0f ? audio::kHighPassOff : highPass, kGlide);
        audio::EchoParams echo = a.echo(bus);
        bool changed = ImGui::SliderFloat("Echo mix", &echo.mix, 0.0f, 1.0f, echo.mix <= 0.0f ? "off" : "%.2f");
        changed |= ImGui::SliderFloat("Echo delay", &echo.delaySeconds, 0.05f, 2.0f, "%.2f s");
        changed |= ImGui::SliderFloat("Echo feedback", &echo.feedback, 0.0f, 0.95f);
        if (changed)
            a.setEcho(bus, echo, kGlide);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Reverb (sound effects only)")) {
        const char* presets[] = {"Off", "Room", "Hall", "Cave"};
        for (int p = 0; p < 4; ++p) {
            if (p > 0)
                ImGui::SameLine();
            if (ImGui::Button(presets[p]))
                a.setReverb(audio::ReverbParams::preset(audio::ReverbPreset(p)), 0.3f);
        }
        audio::ReverbParams reverb = a.reverb();
        bool changed = ImGui::SliderFloat("Level", &reverb.level, 0.0f, 1.0f, reverb.level <= 0.0f ? "off" : "%.2f");
        changed |= ImGui::SliderFloat("Room size", &reverb.roomSize, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Damping", &reverb.damping, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Width", &reverb.width, 0.0f, 1.0f);
        if (changed)
            a.setReverb(reverb, kGlide);
        ImGui::TreePop();
    }
    if (ImGui::Button("Reset all effects"))
        a.resetEffects();
    ImGui::TreePop();
}

void Application::drawEnginePanel()
{
    const float ui = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2(12.0f * ui, 90.0f * ui), ImGuiCond_FirstUseEver);
    // Fit the contents, but never taller than the screen (it scrolls instead).
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                        ImVec2(10000.0f, ImGui::GetMainViewport()->Size.y - 100.0f * ui));
    if (!ImGui::Begin("Engine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // Frame time graph (oldest on the left).
    float average = 0.0f, worst = 0.0f;
    for (float t : frameTimes_) {
        average += t;
        worst = std::max(worst, t);
    }
    average /= float(frameTimes_.size());
    char overlay[64];
    SDL_snprintf(overlay, sizeof(overlay), "avg %.2f ms   max %.2f ms", double(average), double(worst));
    ImGui::Text("%.0f fps", double(fps_));
    ImGui::PlotLines("##frametimes", frameTimes_.data(), int(frameTimes_.size()), int(frameTimeIndex_), overlay, 0.0f,
                     std::max(34.0f, worst * 1.1f), ImVec2(300.0f * ui, 60.0f * ui));

    if (ImGui::CollapsingHeader("Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        const VkPhysicalDeviceProperties& props = context_->properties();
        const VkExtent2D extent = renderer_->extent();
        ImGui::Text("GPU: %s", props.deviceName);
        ImGui::Text("Vulkan %u.%u.%u", VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion),
                    VK_API_VERSION_PATCH(props.apiVersion));
        ImGui::Text("Swapchain: %u x %u, %s", extent.width, extent.height, formatName(renderer_->colorFormat()));
        ImGui::Text("Depth: %s   Frames in flight: %u", formatName(renderer_->depthFormat()), renderer_->framesInFlight());
        ImGui::Text("Validation: %s", config_.validation ? "on" : "off");
        bool vsync = renderer_->vsync();
        if (ImGui::Checkbox("Vsync", &vsync))
            renderer_->setVsync(vsync);
        ImGui::SameLine();
        bool fullscreen = window_->isFullscreen();
        if (ImGui::Checkbox("Fullscreen", &fullscreen))
            window_->setFullscreen(fullscreen);
    }

    if (ImGui::CollapsingHeader("Audio")) {
        audio::AudioEngine& a = *audio_;
        if (!a.available()) {
            ImGui::TextDisabled("No audio device (or GAME_AUDIO=0)");
        } else {
            ImGui::Text("%u Hz   Sounds playing: %u%s", a.sampleRate(), a.activeSounds(),
                        a.isMusicPlaying() ? "   Music: on" : "");
            bool muted = a.muted();
            if (ImGui::Checkbox("Mute", &muted))
                a.setMuted(muted);
            float master = a.masterVolume(), sfx = a.sfxVolume(), music = a.musicVolume();
            if (ImGui::SliderFloat("Master", &master, 0.0f, 1.0f))
                a.setMasterVolume(master);
            if (ImGui::SliderFloat("Effects", &sfx, 0.0f, 1.0f))
                a.setSfxVolume(sfx);
            if (ImGui::SliderFloat("Music", &music, 0.0f, 1.0f))
                a.setMusicVolume(music);
            drawAudioEffects(a);
        }
    }

    if (ImGui::CollapsingHeader("Input")) {
        const glm::vec2 mouse = input_->mousePosition();
        ImGui::Text("Mouse: %.0f, %.0f (pixels)", double(mouse.x), double(mouse.y));
        if (input_->hasGamepad())
            ImGui::Text("Gamepad: %s", input_->gamepadName());
        else
            ImGui::TextDisabled("No gamepad connected");
    }

    if (ImGui::CollapsingHeader("Settings & saves")) {
        const std::string folder = saveFolder();
        ImGui::TextUnformatted("Save folder:");
        ImGui::TextWrapped("%s", folder.c_str());
        if (ImGui::Button("Open folder")) {
            std::string url = folder;
            std::replace(url.begin(), url.end(), '\\', '/');
            SDL_OpenURL(((url.front() == '/' ? "file://" : "file:///") + url).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Save settings now"))
            saveSettings();
        ImGui::SameLine();
        if (ImGui::Button("Reset settings"))
            resetSettings();
        ImGui::TextDisabled(config_.rememberSettings ? "settings.json: loaded at start, saved at exit"
                                                     : "rememberSettings is off");
    }
    if (ImGui::CollapsingHeader("Game states")) {
        // Top first, as the player sees them.
        if (states_.empty())
            ImGui::TextDisabled("None: just the game");
        for (size_t i = states_.size(); i > 0; --i) {
            const GameState& s = *states_.at(i - 1);
            ImGui::BulletText("%s%s%s%s", s.name.c_str(), states_.isActive(s) ? "" : " (paused)",
                              s.pausesBelow ? ", pauses below" : "", s.hidesBelow ? ", hides below" : "");
        }
        ImGui::BulletText("Game%s", states_.gamePaused() ? " (paused)" : "");
    }

    ImGui::Separator();
    ImGui::Checkbox("Dear ImGui demo window", &showImGuiDemo_);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Every ImGui widget, with the code that makes it.\n"
                               "The best reference while writing your own panels.");
        ImGui::EndTooltip();
    }
    ImGui::End();

    if (showImGuiDemo_)
        ImGui::ShowDemoWindow(&showImGuiDemo_);
}

} // namespace core
