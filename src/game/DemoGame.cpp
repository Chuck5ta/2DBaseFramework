#include "game/DemoGame.h"

#include "core/Paths.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

using namespace render2d;

namespace {

constexpr float kTileSize = 32.0f;
constexpr int kWorldTiles = 64;                      // world is 64x64 tiles
constexpr float kWorldSize = kTileSize * kWorldTiles;
constexpr float kPlayerSpeed = 180.0f;               // world units / second
constexpr float kPlayerSize = 32.0f;
constexpr int kCoinCount = 24;

// Procedural textures so the demo works without any art. Real games load
// files: gfx::Texture::fromFile(gpu, core::assetPath("textures/x.png")).
gfx::Texture makeTileTexture(gfx::VulkanContext& gpu)
{
    constexpr int N = 32;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const bool edge = x == 0 || y == 0;
            const uint8_t v = edge ? 200 : 235;
            uint8_t* p = &px[(y * N + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    gfx::TextureDesc desc;
    desc.filter = VK_FILTER_NEAREST;
    desc.mipmaps = false;
    desc.debugName = "tile";
    return gfx::Texture::fromPixels(gpu, N, N, px.data(), desc);
}

gfx::Texture makeOrbTexture(gfx::VulkanContext& gpu)
{
    constexpr int N = 64;
    std::vector<uint8_t> px(N * N * 4);
    for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            const float dx = (float(x) + 0.5f) / float(N) * 2.0f - 1.0f;
            const float dy = (float(y) + 0.5f) / float(N) * 2.0f - 1.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float alpha = std::clamp((1.0f - d) * 8.0f, 0.0f, 1.0f); // anti-aliased edge
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

} // namespace

void DemoGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    batch_ = std::make_unique<SpriteBatch>(gpu, app.renderer());

    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST;
    pixelArt.mipmaps = false;
    pixelArt.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    playerTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/player.png"), pixelArt);
    tileTexture_ = makeTileTexture(gpu);
    orbTexture_ = makeOrbTexture(gpu);

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;
    camera_.zoom = 1.0f;

    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    camera_.position = playerPos_;

    for (int i = 0; i < kCoinCount; ++i)
        coins_.push_back({randomWorldPoint(), float(i)});

    LOG_INFO("Controls: WASD/arrows/left stick move, Space/(A) burst, wheel/bumpers zoom, "
             "V vsync, F11 fullscreen, Esc/(Back) quit");
}

void DemoGame::onShutdown(core::Application&)
{
    // GPU is idle here. Destroy GPU resources explicitly (or let destructors do it).
    batch_.reset();
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
    time_ += dt;

    // --- Player movement (simulation runs at a fixed 60 Hz) ---
    prevPlayerPos_ = playerPos_;
    const glm::vec2 targetVel = moveInput_ * kPlayerSpeed;
    // Exponential approach gives a little acceleration/deceleration.
    playerVel_ += (targetVel - playerVel_) * (1.0f - std::exp(-12.0f * dt));
    playerPos_ += playerVel_ * dt;
    playerPos_ = glm::clamp(playerPos_, glm::vec2(kPlayerSize * 0.5f), glm::vec2(kWorldSize - kPlayerSize * 0.5f));
    if (std::abs(playerVel_.x) > 5.0f)
        facingLeft_ = playerVel_.x < 0.0f;

    // --- Coins ---
    for (Coin& c : coins_) {
        if (glm::distance(c.position, playerPos_) < 24.0f) {
            spawnBurst(c.position, 30, colors::Yellow);
            c.position = randomWorldPoint();
            ++score_;
            app.input().rumble(0.3f, 0.6f, 80);
        }
    }

    // --- Particles ---
    for (Particle& p : particles_) {
        p.position += p.velocity * dt;
        p.velocity *= std::exp(-3.0f * dt);
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life <= 0.0f; }),
                     particles_.end());
}

void DemoGame::onUpdate(core::Application& app, float dt)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
    if (in.keyPressed(SDL_SCANCODE_V))
        app.renderer().setVsync(!app.renderer().vsync());

    // --- Movement input: keyboard + gamepad, read every frame, used by the fixed step ---
    glm::vec2 move(0.0f);
    if (in.keyDown(SDL_SCANCODE_A) || in.keyDown(SDL_SCANCODE_LEFT))  move.x -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_D) || in.keyDown(SDL_SCANCODE_RIGHT)) move.x += 1.0f;
    if (in.keyDown(SDL_SCANCODE_W) || in.keyDown(SDL_SCANCODE_UP))    move.y -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_S) || in.keyDown(SDL_SCANCODE_DOWN))  move.y += 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_LEFT))  move.x -= 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) move.x += 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_UP))    move.y -= 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_DOWN))  move.y += 1.0f;
    if (glm::length(move) > 1.0f)
        move = glm::normalize(move);
    const glm::vec2 stick = in.leftStick();
    if (glm::length(stick) > glm::length(move))
        move = stick; // analogue: partial tilt = slower walk
    moveInput_ = move;

    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH))
        spawnBurst(playerPos_, 60, colors::Blue);

    // --- Zoom ---
    float zoomDelta = in.mouseWheel() * 0.1f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) zoomDelta += 1.5f * dt;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  zoomDelta -= 1.5f * dt;
    if (in.keyDown(SDL_SCANCODE_EQUALS)) zoomDelta += 1.5f * dt;
    if (in.keyDown(SDL_SCANCODE_MINUS))  zoomDelta -= 1.5f * dt;
    camera_.zoom = std::clamp(camera_.zoom * std::exp(zoomDelta), 0.25f, 4.0f);

    // --- Camera follows the (interpolated) player smoothly ---
    const glm::vec2 renderPos = glm::mix(prevPlayerPos_, playerPos_, app.interpolationAlpha());
    camera_.position += (renderPos - camera_.position) * (1.0f - std::exp(-6.0f * dt));
}

void DemoGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    camera_.setViewport(viewport.x, viewport.y);

    // ===== World layer =====
    batch_->begin(frame, camera_.viewProjection());

    // Tiles (only the visible ones)
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
            tile.origin = glm::vec2(0.0f);
            const bool checker = ((x + y) & 1) != 0;
            tile.color = checker ? Color(0.36f, 0.42f, 0.52f, 1.0f) : Color(0.32f, 0.38f, 0.48f, 1.0f);
            batch_->draw(tileTexture_, tile);
        }

    // World border
    batch_->drawRectOutline({0.0f, 0.0f}, glm::vec2(kWorldSize), 4.0f, Color(0.1f, 0.1f, 0.15f, 1.0f));

    // Coins: bob up and down, pulse
    const float t = float(app.time());
    for (const Coin& c : coins_) {
        Sprite s;
        s.position = c.position + glm::vec2(0.0f, std::sin(t * 3.0f + c.phase) * 3.0f);
        s.size = glm::vec2(14.0f + std::sin(t * 6.0f + c.phase) * 1.5f);
        s.color = colors::Yellow;
        batch_->draw(orbTexture_, s);
    }

    // Particles
    for (const Particle& p : particles_) {
        const float k = p.life / p.maxLife;
        Sprite s;
        s.position = p.position;
        s.size = glm::vec2(8.0f * k + 2.0f);
        s.color = Color(p.color.r, p.color.g, p.color.b, k);
        batch_->draw(orbTexture_, s);
    }

    // Player (interpolated between fixed steps for smooth motion at any refresh rate)
    Sprite player;
    player.position = glm::mix(prevPlayerPos_, playerPos_, app.interpolationAlpha());
    player.size = glm::vec2(kPlayerSize);
    player.uv = facingLeft_ ? UvRect{}.flippedX() : UvRect{};
    // Little squash & stretch while moving
    const float speed01 = std::min(glm::length(playerVel_) / kPlayerSpeed, 1.0f);
    player.size.y *= 1.0f + 0.08f * speed01 * std::sin(t * 18.0f);
    player.origin = glm::vec2(0.5f, 0.8f);
    batch_->draw(playerTexture_, player);

    batch_->end();

    // ===== HUD layer (screen pixels, independent of the world camera) =====
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f; // world == screen pixels
    batch_->begin(frame, hudCamera_.viewProjection());

    const float ui = std::max(1.0f, viewport.y / 720.0f); // UI scale
    const int shown = std::min(score_, 40);
    for (int i = 0; i < shown; ++i)
        batch_->draw(orbTexture_, glm::vec2(24.0f * ui + float(i) * 18.0f * ui, 24.0f * ui), 0.22f * ui, 0.0f,
                     colors::Yellow);

    // Gamepad indicator (green square when a pad / the Deck controls are active)
    batch_->drawRect({viewport.x - 30.0f * ui, 14.0f * ui}, glm::vec2(16.0f * ui),
                     app.input().hasGamepad() ? colors::Green : Color(0.3f, 0.3f, 0.3f, 0.6f));

    batch_->end();
}
