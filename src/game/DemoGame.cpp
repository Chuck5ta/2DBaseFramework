#include "game/DemoGame.h"

#include "core/Paths.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

using namespace render2d;

namespace {

constexpr float kPlayerSpeed = 180.0f; // world units per second
constexpr float kPlayerSize = 32.0f;
constexpr float kTileSize = 32.0f;
constexpr int kWorldTiles = 64;        // the world is 64 x 64 tiles
constexpr float kWorldSize = kTileSize * kWorldTiles;
constexpr int kCoinCount = 24;

// Sound zones near the start: reverb inside the cave, muffled sound in the pond.
struct Zone {
    glm::vec2 min, max; // world units
    bool contains(glm::vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
};
constexpr Zone kCave{{20 * kTileSize, 26 * kTileSize}, {28 * kTileSize, 38 * kTileSize}};
constexpr Zone kPond{{36 * kTileSize, 28 * kTileSize}, {45 * kTileSize, 36 * kTileSize}};

// A 32x32 grey tile with a slightly darker top/left edge, generated in code.
// Tinting it with two colours gives a checkerboard.
gfx::Texture makeTileTexture(gfx::VulkanContext& gpu)
{
    constexpr int N = 32;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool edge = x == 0 || y == 0;
            const uint8_t v = edge ? 200 : 235;
            uint8_t* p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = v; // R, G, B
            p[3] = 255;             // A
        }
    gfx::TextureDesc desc;
    desc.filter = VK_FILTER_NEAREST;
    desc.mipmaps = false;
    desc.debugName = "tile";
    return gfx::Texture::fromPixels(gpu, N, N, px.data(), desc);
}

// A soft white circle with an anti-aliased edge. Tinted, it becomes coins,
// particles and HUD icons.
gfx::Texture makeOrbTexture(gfx::VulkanContext& gpu)
{
    constexpr int N = 64;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (float(x) + 0.5f) / float(N) * 2.0f - 1.0f; // -1..1
            const float dy = (float(y) + 0.5f) / float(N) * 2.0f - 1.0f;
            const float d = std::sqrt(dx * dx + dy * dy);                  // distance from centre
            const float alpha = std::clamp((1.0f - d) * 8.0f, 0.0f, 1.0f);
            const float shade = std::clamp(1.0f - 0.3f * std::sqrt((dx + 0.35f) * (dx + 0.35f) +
                                                                  (dy + 0.35f) * (dy + 0.35f)), 0.7f, 1.0f);
            uint8_t* p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = uint8_t(shade * 255.0f);
            p[3] = uint8_t(alpha * 255.0f);
        }
    gfx::TextureDesc desc;
    desc.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    desc.debugName = "orb";
    return gfx::Texture::fromPixels(gpu, N, N, px.data(), desc);
}

// "W/A/S/D", or "Left Stick Up/Left/Down/Right": the four movement prompts, with
// shared leading words said once.
std::string movePrompt(const core::Actions& actions)
{
    const std::string names[4] = {actions.prompt("up"), actions.prompt("left"), actions.prompt("down"),
                                  actions.prompt("right")};
    size_t common = 0; // length of the shared whole words at the start
    for (size_t i = 0; i < names[0].size(); ++i) {
        bool same = true;
        for (const std::string& n : names)
            same = same && i < n.size() && n[i] == names[0][i];
        if (!same)
            break;
        if (names[0][i] == ' ')
            common = i + 1;
    }
    std::string text = names[0];
    for (int i = 1; i < 4; ++i)
        text += "/" + names[i].substr(common);
    return text;
}

// The help panel's text, built from the bindings: it shows the keys or buttons of
// the device used last, and stays right after rebinding.
std::string helpText(const core::Actions& actions)
{
    const bool pad = actions.lastDevice() == core::InputDevice::Gamepad;
    return movePrompt(actions) + ": move      " + actions.prompt("burst") + ": burst      " +
           (pad ? "" : "Wheel, ") + actions.prompt("zoom in") + "/" + actions.prompt("zoom out") + ": zoom\n" +
           actions.prompt("pause") + ": pause      " +
           actions.prompt("mute") + ": mute      " + actions.prompt("vsync") + ": vsync      " +
           actions.prompt("help") + ": hide help      ` (below Esc): debug UI";
}

constexpr float kRoundSeconds = 60.0f;

// A list of choices, centred on the screen. Keys, gamepad (the "up", "down" and
// "confirm" actions) and mouse (point and click) all work.
class Menu {
public:
    explicit Menu(std::vector<std::string> items) : items_(std::move(items)) {}

    // The index of the item chosen this frame, or -1.
    int update(core::Application& app)
    {
        const core::Actions& actions = app.actions();
        const int count = int(items_.size());
        if (actions.pressed("up"))
            selected_ = (selected_ + count - 1) % count;
        if (actions.pressed("down"))
            selected_ = (selected_ + 1) % count;

        // The mouse picks whatever it moves over (where the items were last drawn).
        const core::Input& in = app.input();
        const glm::vec2 mouse = in.mousePosition();
        for (int i = 0; i < int(boxes_.size()); ++i) {
            const glm::vec4& b = boxes_[size_t(i)]; // x, y, width, height in pixels
            if (mouse.x < b.x || mouse.y < b.y || mouse.x > b.x + b.z || mouse.y > b.y + b.w)
                continue;
            if (in.mouseDelta() != glm::vec2(0.0f))
                selected_ = i;
            if (in.mousePressed(SDL_BUTTON_LEFT))
                return i;
        }
        return actions.pressed("confirm") ? selected_ : -1;
    }

    // Draws the items from `top` downwards (HUD pixels); the chosen one is highlighted.
    void draw(SpriteBatch& batch, const Font& font, glm::vec2 top, float ui)
    {
        TextStyle style;
        style.size = 30.0f * ui;
        style.align = TextAlign::Center;
        style.snapToPixel = true;
        const float rowHeight = 46.0f * ui;
        const glm::vec2 box(260.0f * ui, rowHeight - 6.0f * ui);
        boxes_.clear();
        for (int i = 0; i < int(items_.size()); ++i) {
            const glm::vec2 corner(top.x - box.x * 0.5f, top.y + float(i) * rowHeight);
            boxes_.emplace_back(corner, box);
            const bool chosen = i == selected_;
            if (chosen)
                batch.drawRect(corner, box, Color(1.0f, 1.0f, 1.0f, 0.15f));
            style.color = chosen ? colors::Yellow : colors::White;
            const float textY = corner.y + (box.y - font.measure(items_[size_t(i)], style.size).y) * 0.5f;
            batch.drawText(font, items_[size_t(i)], {top.x, textY}, style);
        }
    }

private:
    std::vector<std::string> items_;
    int selected_ = 0;
    std::vector<glm::vec4> boxes_;
};

} // namespace

// --- Game states: the menu screens ---------------------------------------------------
// Each is a core::GameState pushed onto app.states(). They pause the game underneath
// (pausesBelow, the default) but let it show through, dimmed.

// What the three screens share: the dimmed game, a heading, a line of text, a menu.
class MenuState : public core::GameState {
protected:
    MenuState(DemoGame& game, std::vector<std::string> items) : game_(game), menu_(std::move(items)) {}

    void drawScreen(core::Application& app, gfx::FrameContext& frame, const std::string& heading,
                    const std::string& text)
    {
        const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
        const float ui = std::max(1.0f, viewport.y / 720.0f);
        game_.hudCamera_.setViewport(viewport.x, viewport.y);
        game_.hudCamera_.position = viewport * 0.5f;
        SpriteBatch& batch = *game_.batch_;
        batch.begin(frame, game_.hudCamera_.viewProjection());

        // Fade the game out over a fifth of a second.
        const float fade = std::min(timeInState() / 0.2f, 1.0f);
        batch.drawRect({0.0f, 0.0f}, viewport, Color(0.02f, 0.03f, 0.06f, 0.65f * fade));

        TextStyle style;
        style.align = TextAlign::Center;
        style.snapToPixel = true;
        style.size = 64.0f * ui;
        batch.drawText(game_.font_, heading, {viewport.x * 0.5f, viewport.y * 0.22f}, style);
        style.size = 24.0f * ui;
        style.color = Color(1.0f, 1.0f, 1.0f, 0.8f);
        batch.drawText(game_.font_, text, {viewport.x * 0.5f, viewport.y * 0.22f + 84.0f * ui}, style);

        menu_.draw(batch, game_.font_, {viewport.x * 0.5f, viewport.y * 0.58f}, ui); // below the player

        style.size = 16.0f * ui;
        style.color = Color(1.0f, 1.0f, 1.0f, 0.6f);
        batch.drawText(game_.font_, app.actions().prompt("confirm") + ": select", {viewport.x * 0.5f, viewport.y - 48.0f * ui}, style);
        batch.end();
    }

    DemoGame& game_;
    Menu menu_;
};

class TitleState : public MenuState {
public:
    explicit TitleState(DemoGame& game) : MenuState(game, {"Play", "Quit"}) { name = "Title"; }

    void onUpdate(core::Application& app, float) override
    {
        switch (menu_.update(app)) {
        case 0: // Play
            game_.startRound();
            app.states().pop(); // the game carries on underneath (next frame)
            break;
        case 1:
            app.quit();
            break;
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        const int best = game_.bestScore();
        drawScreen(app, frame, "Slime Garden",
                   best > 0 ? "Best score: " + std::to_string(best)
                            : "Collect all the coins you can in " + std::to_string(int(kRoundSeconds)) + " seconds");
    }
};

class PauseState : public MenuState {
public:
    explicit PauseState(DemoGame& game) : MenuState(game, {"Resume", "Restart", "Quit to title"}) { name = "Pause"; }

    // Muffle every sound while paused, as if heard through a wall.
    void onEnter(core::Application& app) override { app.audio().setLowPass(audio::Bus::Master, 500.0f, 0.2f); }
    void onExit(core::Application& app) override { app.audio().setLowPass(audio::Bus::Master, audio::kLowPassOff, 0.2f); }

    void onUpdate(core::Application& app, float) override
    {
        if (app.actions().pressed("pause") || app.actions().pressed("back")) {
            app.states().pop();
            return;
        }
        switch (menu_.update(app)) {
        case 0:
            app.states().pop();
            break;
        case 1:
            game_.startRound();
            app.states().pop();
            break;
        case 2:
            app.states().replace<TitleState>(game_); // swap this screen for the title
            break;
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        drawScreen(app, frame, "Paused", "");
    }
};

class RoundOverState : public MenuState {
public:
    RoundOverState(DemoGame& game, int score, bool newBest)
        : MenuState(game, {"Play again", "Title"}), score_(score), newBest_(newBest)
    {
        name = "Round over";
    }

    void onUpdate(core::Application& app, float) override
    {
        // Ignore input for a moment, so a button still being mashed when time runs out
        // doesn't skip the result.
        if (timeInState() < 0.6f)
            return;
        switch (menu_.update(app)) {
        case 0:
            game_.startRound();
            app.states().pop();
            break;
        case 1:
            app.states().replace<TitleState>(game_);
            break;
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        drawScreen(app, frame, "Time's up!",
                   "Score: " + std::to_string(score_) +
                       (newBest_ ? "   New best!" : "   Best: " + std::to_string(game_.bestScore())));
    }

private:
    int score_;
    bool newBest_;
};

void DemoGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    batch_ = std::make_unique<SpriteBatch>(gpu, app.renderer());

    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST; // keep pixels crisp when scaled up
    pixelArt.mipmaps = false;
    pixelArt.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    playerTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/player.png"), pixelArt);
    // Animated sprites. The slime sheet is an Aseprite export: its tags (idle, hop,
    // burst) become clips. The coin is a plain row of 16x16 frames.
    slimeSheet_ = SpriteSheet::loadAseprite(gpu, core::assetPath("sprites/slime.json"));
    slimeSheet_.clip("hop")->addEvent(5, "land"); // frame 5: the slime hits the ground
    coinSheet_ = SpriteSheet::loadGrid(gpu, core::assetPath("sprites/coin.png"), {16, 16});
    playerAnim_.play(slimeSheet_, "idle");
    tileTexture_ = makeTileTexture(gpu);
    orbTexture_ = makeOrbTexture(gpu);
    font_ = Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf")); // baked at 48 px

    // Sounds: decode them now so the first play has no delay.
    coinSound_ = core::assetPath("sounds/coin.wav");
    whooshSound_ = core::assetPath("sounds/whoosh.wav");
    app.audio().preload(coinSound_);
    app.audio().preload(whooshSound_);

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;

    // Start in the middle of the world, with the camera already there.
    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    camera_.position = playerPos_;

    for (int i = 0; i < kCoinCount; ++i)
        coins_.push_back({randomWorldPoint(), float(i)});

    // Input actions: the game asks about "left" or "burst", never about a key or button.
    // These are the default bindings; the player can change them (see the debug UI).
    using core::Binding;
    core::Actions& actions = app.actions();
    actions.define("left", {Binding::key(SDL_SCANCODE_A), Binding::key(SDL_SCANCODE_LEFT),
                            Binding::axis(SDL_GAMEPAD_AXIS_LEFTX, -1), Binding::button(SDL_GAMEPAD_BUTTON_DPAD_LEFT)});
    actions.define("right", {Binding::key(SDL_SCANCODE_D), Binding::key(SDL_SCANCODE_RIGHT),
                             Binding::axis(SDL_GAMEPAD_AXIS_LEFTX, +1), Binding::button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)});
    actions.define("up", {Binding::key(SDL_SCANCODE_W), Binding::key(SDL_SCANCODE_UP),
                          Binding::axis(SDL_GAMEPAD_AXIS_LEFTY, -1), Binding::button(SDL_GAMEPAD_BUTTON_DPAD_UP)});
    actions.define("down", {Binding::key(SDL_SCANCODE_S), Binding::key(SDL_SCANCODE_DOWN),
                            Binding::axis(SDL_GAMEPAD_AXIS_LEFTY, +1), Binding::button(SDL_GAMEPAD_BUTTON_DPAD_DOWN)});
    actions.define("burst", {Binding::key(SDL_SCANCODE_SPACE), Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)});
    actions.define("zoom in", {Binding::key(SDL_SCANCODE_EQUALS), Binding::button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)});
    actions.define("zoom out", {Binding::key(SDL_SCANCODE_MINUS), Binding::button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)});
    actions.define("mute", {Binding::key(SDL_SCANCODE_M)});
    actions.define("vsync", {Binding::key(SDL_SCANCODE_V)});
    actions.define("help", {Binding::key(SDL_SCANCODE_H)});
    actions.define("pause", {Binding::key(SDL_SCANCODE_ESCAPE), Binding::button(SDL_GAMEPAD_BUTTON_START)});
    actions.define("confirm", {Binding::key(SDL_SCANCODE_RETURN), Binding::key(SDL_SCANCODE_SPACE),
                               Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)});
    actions.define("back", {Binding::key(SDL_SCANCODE_BACKSPACE), Binding::button(SDL_GAMEPAD_BUTTON_EAST)});

    // Start on the title screen, over the (paused) game.
    app.states().push<TitleState>(*this);
}

void DemoGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
    batch_.reset();
}

// A fresh round: score, clock, player and coins back to the start.
void DemoGame::startRound()
{
    score_ = 0;
    roundTime_ = kRoundSeconds;
    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    playerVel_ = glm::vec2(0.0f);
    for (Coin& c : coins_)
        c.position = randomWorldPoint();
    particles_.clear();
    popups_.clear();
    bursting_ = false;
}

// A menu screen was pushed: onFixedUpdate and onUpdate stop until it's gone. Rendering
// still blends the last two steps by interpolationAlpha(), which keeps changing, so the
// frozen player would shimmer by a pixel or two. Make both steps the same.
void DemoGame::onPause(core::Application&)
{
    prevPlayerPos_ = playerPos_;
}

glm::vec2 DemoGame::randomWorldPoint()
{
    std::uniform_real_distribution<float> d(kTileSize * 2.0f, kWorldSize - kTileSize * 2.0f);
    return {d(rng_), d(rng_)};
}

void DemoGame::spawnBurst(glm::vec2 at, int count, Color color)
{
    std::uniform_real_distribution<float> angle(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> speed(40.0f, 220.0f);
    std::uniform_real_distribution<float> life(0.4f, 1.0f);
    for (int i = 0; i < count; ++i) {
        const float a = angle(rng_);
        const float l = life(rng_);
        particles_.push_back({at, glm::vec2(std::cos(a), std::sin(a)) * speed(rng_), l, l, color});
    }
}

void DemoGame::onFixedUpdate(core::Application& app, float dt)
{
    // --- The round's clock: when it runs out, the "Time's up" screen takes over ---
    if (roundTime_ > 0.0f) {
        roundTime_ -= dt;
        if (roundTime_ <= 0.0f) {
            roundTime_ = 0.0f;
            const bool newBest = score_ > bestScore_;
            bestScore_ = std::max(bestScore_, score_);
            app.states().push<RoundOverState>(*this, score_, newBest);
            return; // (the push takes effect next frame; stop this step here)
        }
    }

    // Remember where we were, so rendering can interpolate between steps.
    prevPlayerPos_ = playerPos_;

    // Ease the velocity towards the target: a little acceleration/deceleration.
    const glm::vec2 targetVel = moveInput_ * kPlayerSpeed;
    playerVel_ += (targetVel - playerVel_) * (1.0f - std::exp(-12.0f * dt));
    playerPos_ += playerVel_ * dt;
    playerPos_ = glm::clamp(playerPos_, glm::vec2(kPlayerSize * 0.5f), glm::vec2(kWorldSize - kPlayerSize * 0.5f));

    if (std::abs(playerVel_.x) > 5.0f)
        facingLeft_ = playerVel_.x < 0.0f;

    // --- Coins: collect when close enough ---
    for (Coin& c : coins_) {
        if (glm::distance(c.position, playerPos_) < 24.0f) {
            spawnBurst(c.position, 30, colors::Yellow);
            popups_.push_back({c.position, 1.0f}); // "+1" floats up from the coin
            {
                // Pan by where the coin is on screen; vary the pitch a little so
                // repeated pickups don't sound identical.
                const float halfWidth = camera_.viewport().x * 0.5f / camera_.scale();
                std::uniform_real_distribution<float> pitch(0.95f, 1.1f);
                audio::PlayParams sound;
                sound.pan = std::clamp((c.position.x - camera_.position.x) / halfWidth, -1.0f, 1.0f) * 0.8f;
                sound.pitch = pitch(rng_);
                app.audio().play(coinSound_, sound);
            }
            c.position = randomWorldPoint(); // respawn somewhere else
            ++score_;
            app.input().rumble(0.3f, 0.6f, 80);
        }
    }

    // --- Particles: move, slow down, age, and remove dead ones ---
    for (Particle& p : particles_) {
        p.position += p.velocity * dt;
        p.velocity *= std::exp(-3.0f * dt);
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life <= 0.0f; }),
                     particles_.end());

    // --- "+1" popups: rise and fade over about a second ---
    for (Popup& p : popups_) {
        p.position.y -= 30.0f * dt;
        p.life -= dt;
    }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0.0f; }),
                  popups_.end());

    // --- Animation: pick the clip from what the player is doing, then advance it ---
    if (bursting_ && playerAnim_.finished())
        bursting_ = false; // the burst is over: back to idle or hopping
    if (!bursting_)
        playerAnim_.play(slimeSheet_, glm::length(playerVel_) > 30.0f ? "hop" : "idle");
    playerAnim_.update(dt);

    // A puff of dust each time the slime lands (the "land" event on the hop clip).
    if (playerAnim_.eventFired("land")) {
        std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
        for (int i = 0; i < 6; ++i) {
            const glm::vec2 velocity(spread(rng_) * 40.0f, -10.0f - std::abs(spread(rng_)) * 15.0f);
            particles_.push_back({playerPos_ + glm::vec2(spread(rng_) * 8.0f, 5.0f), velocity, 0.35f, 0.35f,
                                  Color(0.85f, 0.8f, 0.7f, 0.8f)});
        }
    }
}

void DemoGame::onUpdate(core::Application& app, float dt)
{
    const core::Input& in = app.input();
    core::Actions& actions = app.actions();

    // Waiting for a new input for an action (the "+" buttons in the debug UI)? The
    // next key, click, button or stick push is it; the game ignores it meanwhile.
    if (!rebinding_.empty()) {
        if (const std::optional<core::Binding> input = actions.capture()) {
            if (*input == core::Binding::key(SDL_SCANCODE_ESCAPE)) {
                rebindNote_ = "Cancelled.";
            } else {
                const std::vector<std::string> users = actions.actionsUsing(*input);
                rebindNote_ = actions.displayName(*input) + " added to \"" + rebinding_ + "\".";
                for (const std::string& other : users)
                    if (other != rebinding_)
                        rebindNote_ += " (Also used by \"" + other + "\".)"; // allowed, but worth knowing
                actions.addBinding(rebinding_, *input);
            }
            rebinding_.clear();
        }
        moveInput_ = glm::vec2(0.0f);
        return;
    }

    if (actions.pressed("pause"))
        app.states().push<PauseState>(*this); // on top from the next frame; the game stops
    if (actions.pressed("vsync"))
        app.renderer().setVsync(!app.renderer().vsync());
    if (actions.pressed("help"))
        showHelp_ = !showHelp_;
    if (actions.pressed("mute"))
        app.audio().setMuted(!app.audio().muted());

    // --- Movement: four actions make one direction (analog with a stick) ---
    moveInput_ = actions.vector("left", "right", "up", "down");

    if (actions.pressed("burst")) {
        spawnBurst(playerPos_, 60, colors::Blue);
        app.audio().play(whooshSound_, {.volume = 0.8f});
        playerAnim_.play(slimeSheet_, "burst", true); // restart, even if already bursting
        bursting_ = true;
    }

    // --- Zoom: mouse wheel, +/- keys, gamepad bumpers ---
    // (The wheel isn't an action: it has no "held" state, just an amount per frame.)
    const float zoomDelta = in.mouseWheel() * 0.1f + (actions.value("zoom in") - actions.value("zoom out")) * 1.5f * dt;
    camera_.zoom = std::clamp(camera_.zoom * std::exp(zoomDelta), 0.25f, 4.0f);

    // --- Camera smoothly follows the (interpolated) player ---
    const glm::vec2 renderPos = glm::mix(prevPlayerPos_, playerPos_, app.interpolationAlpha());
    camera_.position += (renderPos - camera_.position) * (1.0f - std::exp(-6.0f * dt));

    // --- Sound zones: change the effects only when the player enters or leaves ---
    const bool inCave = kCave.contains(playerPos_);
    if (inCave != inCave_) {
        inCave_ = inCave;
        const auto preset = inCave ? audio::ReverbPreset::Cave : audio::ReverbPreset::Off;
        app.audio().setReverb(audio::ReverbParams::preset(preset), 0.6f); // fade in/out over 0.6 s
    }
    const bool inPond = kPond.contains(playerPos_);
    if (inPond != inPond_) {
        inPond_ = inPond;
        // Under water: only the low frequencies get through.
        app.audio().setLowPass(audio::Bus::Sfx, inPond ? 700.0f : audio::kLowPassOff, 0.4f);
    }
}

void DemoGame::onDebugUI(core::Application& app)
{
    // Our own window, at the top right the first time it appears, sized to fit
    // its contents (but no taller than the screen). The framework's "Engine"
    // window is drawn separately.
    const float scale = ImGui::GetStyle().FontScaleDpi; // matches the screen's DPI setting
    const ImVec2 screen = ImGui::GetMainViewport()->Size;
    ImGui::SetNextWindowPos(ImVec2(screen.x - 12.0f * scale, 90.0f * scale), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(screen.x, screen.y - 100.0f * scale));
    if (!ImGui::Begin("Game", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) { // collapsed: skip the contents
        ImGui::End();
        return;
    }

    ImGui::Text("Score: %d   Best: %d   Time left: %.1f s", score_, bestScore_, double(roundTime_));
    ImGui::Text("Coins: %d   Particles: %d   Popups: %d", int(coins_.size()), int(particles_.size()),
                int(popups_.size()));

    ImGui::SeparatorText("Player");
    // Dragging the position teleports the player. Set the previous position too,
    // or interpolation would show it sliding there.
    if (ImGui::DragFloat2("Position", &playerPos_.x, 1.0f, 0.0f, kWorldSize, "%.0f"))
        prevPlayerPos_ = playerPos_;
    ImGui::Text("Velocity: %.0f, %.0f", double(playerVel_.x), double(playerVel_.y));
    ImGui::Text("Sound zone: %s", inCave_ ? "cave (reverb)" : inPond_ ? "pond (muffled)" : "none");
    if (ImGui::Button("Go to the cave"))
        playerPos_ = prevPlayerPos_ = (kCave.min + kCave.max) * 0.5f;
    ImGui::SameLine();
    if (ImGui::Button("Go to the pond"))
        playerPos_ = prevPlayerPos_ = (kPond.min + kPond.max) * 0.5f;
    ImGui::Text("Animation: %s, frame %d", playerAnim_.clipName().c_str(), playerAnim_.frame());
    ImGui::SliderFloat("Animation speed", &playerAnim_.speed, 0.0f, 3.0f, "%.2fx");

    ImGui::SeparatorText("Camera");
    ImGui::SliderFloat("Zoom", &camera_.zoom, 0.25f, 4.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Actions");
    if (ImGui::Button("Burst"))
        spawnBurst(playerPos_, 60, colors::Blue);
    ImGui::SameLine();
    if (ImGui::Button("Scatter coins"))
        for (Coin& c : coins_)
            c.position = randomWorldPoint();
    ImGui::SameLine();
    if (ImGui::Button("Reset score"))
        score_ = 0;
    ImGui::SameLine();
    if (ImGui::Button("End round")) // quicker than waiting a minute to test the result screen
        roundTime_ = std::min(roundTime_, 0.01f);
    ImGui::Checkbox("Show pickup range", &showPickupRange_);

    // Our own textures, shown inside the UI.
    if (ImGui::CollapsingHeader("Textures")) {
        debugui::DebugUI& ui = *app.debugUI(); // never null inside onDebugUI
        const ImVec2 size(64.0f * scale, 64.0f * scale);
        ImGui::Image(ui.texture(playerTexture_), size);
        ImGui::SameLine();
        ImGui::Image(ui.texture(tileTexture_), size);
        ImGui::SameLine();
        ImGui::Image(ui.texture(orbTexture_), size);
        ImGui::Image(ui.texture(slimeSheet_.texture()), ImVec2(224.0f * scale, 64.0f * scale));
        ImGui::Image(ui.texture(coinSheet_.texture()), ImVec2(128.0f * scale, 16.0f * scale));
        ImGui::Text("Font atlas (%u x %u):", font_.texture().width(), font_.texture().height());
        ImGui::Image(ui.texture(font_.texture()), ImVec2(256.0f * scale, 256.0f * scale));
    }
    ImGui::End();

    // Rebinding, in a window of its own: every action with its bindings. Click a
    // binding to remove it; "+" waits for the next input (onUpdate catches it).
    ImGui::SetNextWindowPos(ImVec2(screen.x * 0.5f, 12.0f * scale), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver); // starts closed: click the title
    if (ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        core::Actions& actions = app.actions();
        for (const std::string& name : actions.names()) {
            ImGui::PushID(name.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine(80.0f * scale);
            const std::vector<core::Binding> bindings = actions.bindings(name); // a copy: removing changes it
            for (size_t i = 0; i < bindings.size(); ++i) {
                ImGui::PushID(int(i));
                if (ImGui::SmallButton(actions.displayName(bindings[i]).c_str()))
                    actions.removeBinding(name, bindings[i]);
                ImGui::SetItemTooltip("Click to remove");
                ImGui::PopID();
                ImGui::SameLine();
            }
            if (rebinding_ == name) {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "press a key or button (Esc cancels)");
            } else if (ImGui::SmallButton("+")) {
                rebinding_ = name;
                rebindNote_.clear();
            }
            ImGui::PopID();
        }
        if (!rebindNote_.empty())
            ImGui::TextUnformatted(rebindNote_.c_str());
        if (ImGui::Button("Reset all to defaults")) {
            actions.resetAllToDefaults();
            rebinding_.clear();
            rebindNote_.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy as JSON")) // the text a settings file would save
            ImGui::SetClipboardText(actions.saveBindings().c_str());
        ImGui::Text("Last used: %s",
                    actions.lastDevice() == core::InputDevice::Gamepad ? "gamepad" : "keyboard & mouse");
    }
    ImGui::End();
}

void DemoGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    camera_.setViewport(viewport.x, viewport.y);
    const float t = float(app.time());

    batch_->begin(frame, camera_.viewProjection());

    // --- Tiles: only the ones the camera can see ---
    glm::vec2 vmin, vmax;
    camera_.visibleBounds(vmin, vmax);
    const int x0 = std::max(0, int(std::floor(vmin.x / kTileSize)));
    const int y0 = std::max(0, int(std::floor(vmin.y / kTileSize)));
    const int x1 = std::min(kWorldTiles - 1, int(std::floor(vmax.x / kTileSize)));
    const int y1 = std::min(kWorldTiles - 1, int(std::floor(vmax.y / kTileSize)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            Sprite tile;
            tile.position = glm::vec2(x, y) * kTileSize;
            tile.size = glm::vec2(kTileSize);
            tile.origin = glm::vec2(0.0f); // position is the top-left corner
            const bool checker = ((x + y) & 1) != 0;
            tile.color = checker ? Color(0.36f, 0.42f, 0.52f, 1.0f) : Color(0.32f, 0.38f, 0.48f, 1.0f);
            batch_->draw(tileTexture_, tile);
        }

    // --- World border ---
    batch_->drawRectOutline({0.0f, 0.0f}, glm::vec2(kWorldSize), 4.0f, Color(0.1f, 0.1f, 0.15f, 1.0f));

    // --- Sound zones: a dark cave and a pond, with a label at the top of each ---
    batch_->drawRect(kCave.min, kCave.max - kCave.min, Color(0.05f, 0.04f, 0.08f, 0.6f));
    batch_->drawRectOutline(kCave.min, kCave.max - kCave.min, 3.0f, Color(0.02f, 0.02f, 0.03f, 0.9f));
    batch_->drawRect(kPond.min, kPond.max - kPond.min, Color(0.15f, 0.45f, 0.95f, 0.45f));
    batch_->drawRectOutline(kPond.min, kPond.max - kPond.min, 3.0f, Color(0.6f, 0.85f, 1.0f, 0.8f));
    TextStyle label;
    label.size = 18.0f;
    label.align = TextAlign::Center;
    label.color = Color(1.0f, 1.0f, 1.0f, 0.6f);
    batch_->drawText(font_, "CAVE", {(kCave.min.x + kCave.max.x) * 0.5f, kCave.min.y + 8.0f}, label);
    batch_->drawText(font_, "POND", {(kPond.min.x + kPond.max.x) * 0.5f, kPond.min.y + 8.0f}, label);

    // --- Coins: bob up and down and spin ---
    // Lots of identical looping objects don't need an Animator each: pick the frame
    // from the time (offset per coin, so they don't all spin in step).
    for (const Coin& c : coins_) {
        const int coinFrame = int(t * 12.0f + c.phase * 2.0f) % coinSheet_.frameCount();
        Sprite s;
        s.position = c.position + glm::vec2(0.0f, std::sin(t * 3.0f + c.phase) * 3.0f);
        s.size = glm::vec2(16.0f);
        s.uv = coinSheet_.frame(coinFrame).uv;
        batch_->draw(coinSheet_.texture(), s);
    }

    // --- Particles: shrink and fade out as they age ---
    for (const Particle& p : particles_) {
        const float k = p.life / p.maxLife; // 1 = new, 0 = dead
        Sprite s;
        s.position = p.position;
        s.size = glm::vec2(8.0f * k + 2.0f);
        s.color = Color(p.color.r, p.color.g, p.color.b, k);
        batch_->draw(orbTexture_, s);
    }

    // --- Player: interpolated between fixed steps for smooth motion ---
    Sprite player;
    player.position = glm::mix(prevPlayerPos_, playerPos_, app.interpolationAlpha());
    player.size = glm::vec2(kPlayerSize);
    player.origin = glm::vec2(0.5f, 0.8f); // pivot near the feet
    // The current frame of the player's animation (the art faces right).
    player.uv = playerAnim_.uv(facingLeft_);
    batch_->draw(slimeSheet_.texture(), player);

    // --- Debug drawing, switched on in the debug UI ---
    if (showPickupRange_) {
        auto circle = [&](glm::vec2 centre, float radius, Color color) {
            constexpr int kSegments = 32;
            for (int i = 0; i < kSegments; ++i) {
                const float a0 = float(i) / kSegments * glm::two_pi<float>();
                const float a1 = float(i + 1) / kSegments * glm::two_pi<float>();
                batch_->drawLine(centre + radius * glm::vec2(std::cos(a0), std::sin(a0)),
                                 centre + radius * glm::vec2(std::cos(a1), std::sin(a1)), 1.5f, color);
            }
        };
        circle(player.position, 24.0f, colors::Green); // the distance tested in onFixedUpdate
        for (const Coin& c : coins_)
            circle(c.position, 3.0f, colors::Red);     // where a coin really is (the sprite bobs)
    }

    // --- "+1" popups: world-space text, drawn last so it's on top ---
    for (const Popup& p : popups_) {
        TextStyle style;
        style.size = 14.0f; // world units
        style.align = TextAlign::Center;
        style.color = Color(1.0f, 0.95f, 0.5f, std::min(p.life * 2.0f, 1.0f));
        batch_->drawText(font_, "+1", p.position - glm::vec2(0.0f, 34.0f), style);
    }

    batch_->end();

    // ===== HUD: a second camera where world units == screen pixels =====
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f;
    batch_->begin(frame, hudCamera_.viewProjection());

    const float ui = std::max(1.0f, viewport.y / 720.0f); // scale UI with resolution
    TextStyle hud;
    hud.size = 26.0f * ui;
    hud.snapToPixel = true; // the HUD camera maps 1 unit to 1 pixel: keep glyphs on whole pixels

    // Score: a coin icon and the count next to it.
    batch_->draw(orbTexture_, glm::vec2(30.0f, 30.0f) * ui, 0.35f * ui, 0.0f, colors::Yellow);
    batch_->drawText(font_, std::to_string(score_), glm::vec2(50.0f, 14.0f) * ui, hud);

    // Top centre: the time left in the round, red for the last ten seconds.
    if (roundTime_ > 0.0f) {
        const int seconds = int(std::ceil(roundTime_));
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%d:%02d", seconds / 60, seconds % 60);
        TextStyle timer = hud;
        timer.align = TextAlign::Center;
        timer.color = seconds <= 10 ? colors::Red : colors::White;
        batch_->drawText(font_, clock, {viewport.x * 0.5f, 14.0f * ui}, timer);
    }

    // Top right: frame rate and the active input device.
    TextStyle info = hud;
    info.size = 16.0f * ui;
    info.align = TextAlign::Right;
    info.color = Color(1.0f, 1.0f, 1.0f, 0.75f);
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.0f fps", double(app.fps()));
    batch_->drawText(font_, fps, {viewport.x - 16.0f * ui, 12.0f * ui}, info);
    const bool pad = app.actions().lastDevice() == core::InputDevice::Gamepad; // the one used last
    info.color = pad ? colors::Green : Color(1.0f, 1.0f, 1.0f, 0.5f);
    batch_->drawText(font_, pad ? "Gamepad" : "Keyboard & mouse", {viewport.x - 16.0f * ui, 34.0f * ui}, info);
    if (app.audio().muted()) {
        info.color = colors::Yellow;
        batch_->drawText(font_, "Muted", {viewport.x - 16.0f * ui, 56.0f * ui}, info);
    }

    // Bottom: a help panel, sized to fit its text.
    if (showHelp_ && !app.states().gamePaused()) { // (menus show their own help)
        const std::string help = helpText(app.actions());
        TextStyle style = hud;
        style.size = 16.0f * ui;
        style.align = TextAlign::Center;
        const glm::vec2 size = font_.measure(help, style.size);
        const glm::vec2 top(viewport.x * 0.5f, viewport.y - size.y - 24.0f * ui);
        const glm::vec2 margin(14.0f * ui, 8.0f * ui);
        batch_->drawRect({top.x - size.x * 0.5f - margin.x, top.y - margin.y}, size + margin * 2.0f,
                         Color(0.0f, 0.0f, 0.0f, 0.5f));
        batch_->drawText(font_, help, top, style);
    }

    batch_->end();
}
