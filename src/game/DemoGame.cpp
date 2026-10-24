#include "game/DemoGame.h"

#include "core/Paths.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using namespace render2d;

namespace {

constexpr float kPlayerSpeed = 180.0f; // world units per second
constexpr float kPlayerSize = 32.0f;
constexpr float kTileSize = 32.0f;
constexpr int kWorldTiles = 64;        // the world is 64 x 64 tiles
constexpr float kWorldSize = kTileSize * kWorldTiles;
constexpr int kCoinCount = 24;

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

} // namespace

void DemoGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    batch_ = std::make_unique<SpriteBatch>(gpu, app.renderer());

    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST; // keep pixels crisp when scaled up
    pixelArt.mipmaps = false;
    pixelArt.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    playerTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/player.png"), pixelArt);
    tileTexture_ = makeTileTexture(gpu);
    orbTexture_ = makeOrbTexture(gpu);
    font_ = Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf")); // baked at 48 px

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;

    // Start in the middle of the world, with the camera already there.
    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    camera_.position = playerPos_;

    for (int i = 0; i < kCoinCount; ++i)
        coins_.push_back({randomWorldPoint(), float(i)});
}

void DemoGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
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
}

void DemoGame::onUpdate(core::Application& app, float dt)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
    if (in.keyPressed(SDL_SCANCODE_V))
        app.renderer().setVsync(!app.renderer().vsync());
    if (in.keyPressed(SDL_SCANCODE_H))
        showHelp_ = !showHelp_;

    // --- Movement input: read every frame, used by the next fixed step ---
    glm::vec2 move(0.0f);
    if (in.keyDown(SDL_SCANCODE_A) || in.keyDown(SDL_SCANCODE_LEFT))  move.x -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_D) || in.keyDown(SDL_SCANCODE_RIGHT)) move.x += 1.0f;
    if (in.keyDown(SDL_SCANCODE_W) || in.keyDown(SDL_SCANCODE_UP))    move.y -= 1.0f; // +Y is down
    if (in.keyDown(SDL_SCANCODE_S) || in.keyDown(SDL_SCANCODE_DOWN))  move.y += 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_LEFT))  move.x -= 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) move.x += 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_UP))    move.y -= 1.0f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_DPAD_DOWN))  move.y += 1.0f;
    if (glm::length(move) > 1.0f)
        move = glm::normalize(move); // diagonals aren't faster
    const glm::vec2 stick = in.leftStick();
    if (glm::length(stick) > glm::length(move))
        move = stick; // analogue: a partial tilt walks slower
    moveInput_ = move;

    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH))
        spawnBurst(playerPos_, 60, colors::Blue);

    // --- Zoom: mouse wheel, +/- keys, gamepad bumpers ---
    float zoomDelta = in.mouseWheel() * 0.1f;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) zoomDelta += 1.5f * dt;
    if (in.gamepadDown(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  zoomDelta -= 1.5f * dt;
    if (in.keyDown(SDL_SCANCODE_EQUALS)) zoomDelta += 1.5f * dt;
    if (in.keyDown(SDL_SCANCODE_MINUS))  zoomDelta -= 1.5f * dt;
    camera_.zoom = std::clamp(camera_.zoom * std::exp(zoomDelta), 0.25f, 4.0f);

    // --- Camera smoothly follows the (interpolated) player ---
    const glm::vec2 renderPos = glm::mix(prevPlayerPos_, playerPos_, app.interpolationAlpha());
    camera_.position += (renderPos - camera_.position) * (1.0f - std::exp(-6.0f * dt));
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

    // --- Coins: bob up and down and pulse ---
    for (const Coin& c : coins_) {
        Sprite s;
        s.position = c.position + glm::vec2(0.0f, std::sin(t * 3.0f + c.phase) * 3.0f);
        s.size = glm::vec2(14.0f + std::sin(t * 6.0f + c.phase) * 1.5f);
        s.color = colors::Yellow;
        batch_->draw(orbTexture_, s);
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
    player.uv = facingLeft_ ? UvRect{}.flippedX() : UvRect{};
    // A little squash & stretch while moving.
    const float speed01 = std::min(glm::length(playerVel_) / kPlayerSpeed, 1.0f);
    player.size.y *= 1.0f + 0.08f * speed01 * std::sin(t * 18.0f);
    batch_->draw(playerTexture_, player);

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

    // Top right: frame rate and the active input device.
    TextStyle info = hud;
    info.size = 16.0f * ui;
    info.align = TextAlign::Right;
    info.color = Color(1.0f, 1.0f, 1.0f, 0.75f);
    char fps[32];
    std::snprintf(fps, sizeof(fps), "%.0f fps", double(app.fps()));
    batch_->drawText(font_, fps, {viewport.x - 16.0f * ui, 12.0f * ui}, info);
    const bool pad = app.input().hasGamepad();
    info.color = pad ? colors::Green : Color(1.0f, 1.0f, 1.0f, 0.5f);
    batch_->drawText(font_, pad ? "Gamepad" : "Keyboard & mouse", {viewport.x - 16.0f * ui, 34.0f * ui}, info);

    // Bottom: a help panel, sized to fit its text.
    if (showHelp_) {
        const char* help = "WASD / left stick: move      Space / (A): burst\n"
                           "Wheel / bumpers: zoom      V: vsync      H: hide help";
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
