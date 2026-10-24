# Game2D — Tutorial: Making the Demo

This tutorial rebuilds the demo game from an empty window, one feature at a time. It uses a
new class, `MyGame`, so the original `DemoGame` stays available for comparison. After
every step you can build and run the game and see the result.

**What you'll learn:** the `Game` hooks, drawing sprites, loading textures, keyboard and
gamepad input, the fixed timestep with interpolation, cameras, culling, simple particles
and a HUD.

**Before you start:** the project builds and runs (see [BUILDING.md](BUILDING.md)). For
background on any class used here, see [ARCHITECTURE.md](ARCHITECTURE.md).

| Step | You add | You see |
|---|---|---|
| [1](#step-1--an-empty-game) | An empty game | A coloured window; Esc quits |
| [2](#step-2--draw-a-sprite) | `SpriteBatch`, `Camera2D`, a texture | The slime in the middle of the screen |
| [3](#step-3--move-with-keyboard-and-gamepad) | Input + fixed-timestep movement | The slime walks around |
| [4](#step-4--a-tiled-world) | A tiled floor, world bounds, culling | A big checkerboard world |
| [5](#step-5--a-camera-that-follows) | Camera follow + zoom | The view tracks the slime |
| [6](#step-6--coins-and-particles) | Coins to collect, particle bursts | Gameplay! |
| [7](#step-7--a-hud) | A second camera for screen-space UI | Score and gamepad indicator |

> Every code listing in this tutorial was compiled (with warnings as errors) and run with
> Vulkan validation enabled, step by step. If something doesn't work, compare with
> `src/game/DemoGame.cpp`: the finished `MyGame` does exactly what `DemoGame` does; the
> code differs only in small details such as line order.

---

## Step 1 — An empty game

A game is a class derived from `core::Game`. `core::Application` owns the window, input
and GPU, runs the main loop and calls your class's hooks (`onInit`, `onUpdate`,
`onRender`, …). Every hook is optional; this first version only uses `onUpdate` to quit.

> **Adding files in Visual Studio.** You never need to edit CMake for new source files:
> anything under `src/` is picked up automatically on the next build. Create the files from
> **File Explorer**, or in Solution Explorer switch to **Folder View** (the *Switch between
> solutions and available views* button) and use *Add → New File* on `src/game`.
> **Avoid *Add → New Item* in the *CMake Targets View*:** Visual Studio then tries to add
> the file to a CMake target itself, and can edit the wrong CMake script (for example
> `cmake/Dependencies.cmake`), which breaks the configure step.

Create **`src/game/MyGame.h`**:

```cpp
#pragma once

#include "core/Application.h"

class MyGame : public core::Game {
public:
    void onUpdate(core::Application& app, float dt) override;
};
```

Create **`src/game/MyGame.cpp`**:

```cpp
#include "game/MyGame.h"

void MyGame::onUpdate(core::Application& app, float /*dt*/)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();
}
```

`keyPressed` is true only on the frame the key goes down; `keyDown` (used later) is true
while it is held. Keys are identified by *scancode* (physical position), so WASD works on
AZERTY keyboards too. On a gamepad (and the Steam Deck), the *View/Back* button quits.

Now point **`src/main.cpp`** at the new class. Replace its contents with:

```cpp
#include "core/Application.h"
#include "game/MyGame.h"

#include <SDL3/SDL_main.h>

int main(int, char**)
{
    core::AppConfig config;
    config.window.title = "MyGame";
    config.renderer.depthBuffer = false;                       // 2D: draw order decides what's on top
    config.renderer.clearColor = {0.10f, 0.12f, 0.20f, 1.0f};  // slate blue (linear colour)

    core::Application app(config);
    MyGame game;
    return app.run(game);
}
```

The clear colour is given in **linear** colour space (the renderer clears the sRGB
swapchain with it), so `0.10, 0.12, 0.20` shows as a slate blue-grey (linear 0.1 is about 0.35 on screen).

**Build and run.** CMake picks up new files in `src/` automatically. You get a window filled
with the clear colour, and the frame rate in the title bar. **Esc** quits; **F11** toggles
fullscreen (handled by `Application`).

---

## Step 2 — Draw a sprite

Three pieces are needed to draw:

- a **`gfx::Texture`**: the image on the GPU;
- a **`render2d::SpriteBatch`**: collects quads and sends them to the GPU in as few draw
  calls as possible;
- a **`render2d::Camera2D`**: decides which part of the world is visible.

GPU resources are created in `onInit` (the GPU is ready by then) and released in
`onShutdown` (the GPU is idle by then).

Update **`MyGame.h`**: add the includes, the two new hooks, and the members:

```cpp
#pragma once

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"

#include <memory>

class MyGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;

private:
    std::unique_ptr<render2d::SpriteBatch> batch_;
    render2d::Camera2D camera_;
    gfx::Texture playerTexture_;
};
```

In **`MyGame.cpp`**, extend the includes:

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

using namespace render2d;
```

and add `onInit`, `onShutdown` and `onRender`:

```cpp
void MyGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    batch_ = std::make_unique<SpriteBatch>(gpu, app.renderer());

    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST; // keep pixels crisp when scaled up
    pixelArt.mipmaps = false;
    pixelArt.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    playerTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/player.png"), pixelArt);

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;
}

void MyGame::onShutdown(core::Application&)
{
    // The GPU is idle here, so it is safe to destroy GPU resources.
    batch_.reset();
}

void MyGame::onRender(core::Application&, gfx::FrameContext& frame)
{
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    camera_.setViewport(viewport.x, viewport.y);

    batch_->begin(frame, camera_.viewProjection());

    // Draw the player texture at its native size, centred on the world origin.
    batch_->draw(playerTexture_, glm::vec2(0.0f));

    batch_->end();
}
```

Things to notice:

- **Pixel art settings.** `VK_FILTER_NEAREST` and no mipmaps keep the 32×32 sprite sharp
  when it's scaled up. For painted/photographic art keep the defaults (linear + mipmaps).
- **`core::assetPath`** resolves `assets/…` next to the executable, so the game works no
  matter where it's started from. The build copies `assets/` there automatically.
- **`referenceHeight = 360`** means the camera always shows 360 world units vertically,
  whatever the window size. On a 720p window one world unit is 2 pixels; on the Deck's
  800p screen it's 2.2. Your game looks the same everywhere.
- **The camera's `position` is the world point at the centre of the screen.** It starts
  at (0, 0), so a sprite drawn at (0, 0) appears in the middle.
- Everything between `begin()` and `end()` becomes one or more GPU draw calls.

**Run it:** the green slime sits in the middle of the window. Resize the window: it stays
centred and keeps its relative size.

---

## Step 3 — Move with keyboard and gamepad

Movement uses two hooks, and the difference between them matters:

| Hook | Called | Use it for |
|---|---|---|
| `onUpdate(dt)` | once per rendered frame | reading input, camera, things that should feel instant |
| `onFixedUpdate(dt)` | at exactly 60 Hz (0..n times per frame) | gameplay simulation: movement, collisions, timers |

The fixed rate makes gameplay behave identically at 30, 60 or 144 fps. The catch: if the
screen refreshes faster than 60 Hz, some frames have no new simulation step, and motion
would look jerky. The fix is **interpolation**: keep the previous position, and draw
`mix(previous, current, app.interpolationAlpha())`, where the alpha says how far we are
between the last two steps.

Add to **`MyGame.h`**: the `onFixedUpdate` declaration next to the other hooks,

```cpp
    void onFixedUpdate(core::Application& app, float dt) override;
```

and the simulation state to the private members:

```cpp
    // Simulation state, advanced in onFixedUpdate. prevPlayerPos_ is the
    // position one step earlier, used to interpolate when rendering.
    glm::vec2 playerPos_{0.0f};
    glm::vec2 prevPlayerPos_{0.0f};
    glm::vec2 playerVel_{0.0f};
    glm::vec2 moveInput_{0.0f}; // written in onUpdate, read in onFixedUpdate
    bool facingLeft_ = false;
```

In **`MyGame.cpp`**, extend the includes:

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

#include <glm/common.hpp>
#include <glm/exponential.hpp>
#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

using namespace render2d;
```

Add a few constants at the top (after the includes):

```cpp
namespace {

constexpr float kPlayerSpeed = 180.0f; // world units per second
constexpr float kPlayerSize = 32.0f;

} // namespace
```

Add the fixed update:

```cpp
void MyGame::onFixedUpdate(core::Application&, float dt)
{
    // Remember where we were, so rendering can interpolate between steps.
    prevPlayerPos_ = playerPos_;

    // Ease the velocity towards the target: a little acceleration/deceleration.
    const glm::vec2 targetVel = moveInput_ * kPlayerSpeed;
    playerVel_ += (targetVel - playerVel_) * (1.0f - std::exp(-12.0f * dt));
    playerPos_ += playerVel_ * dt;

    if (std::abs(playerVel_.x) > 5.0f)
        facingLeft_ = playerVel_.x < 0.0f;
}
```

Read input in `onUpdate`, which now looks like this:

```cpp
void MyGame::onUpdate(core::Application& app, float /*dt*/)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();

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
}
```

And in `onRender`, replace the single `draw` call with a full `Sprite` description, drawn
at the *interpolated* position:

```cpp
void MyGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));
    camera_.setViewport(viewport.x, viewport.y);
    const float t = float(app.time());

    batch_->begin(frame, camera_.viewProjection());

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

    batch_->end();
}
```

Things to notice:

- **+Y points down** in Game2D (like most 2D tools), so *up* is `move.y -= 1`.
- `Sprite::origin` is the pivot, as a fraction of the size: `(0.5, 0.8)` puts the
  position near the slime's feet, which looks natural when it squashes.
- `UvRect{}.flippedX()` mirrors the texture, so the slime faces where it's going.
- `1 - exp(-k·dt)` is a frame-rate independent way to "move a fraction of the way towards a
  target"; you'll see it again for the camera.
- If a gamepad (or the Deck's controls) is connected, the left stick and D-pad work too.
  Partial stick tilt gives slower walking.

**Run it:** WASD / arrow keys / left stick move the slime around the screen, with a little
acceleration and a squash-and-stretch wobble. Note that you can walk off the screen; we fix
that next.

---

## Step 4 — A tiled world

Now the slime needs somewhere to be: a 64×64 grid of 32-unit tiles (2048×2048 world
units). Two ideas are introduced:

- **Generating a texture in code.** `gfx::Texture::fromPixels` takes raw RGBA bytes. One
  grey tile texture, tinted with two colours, makes a checkerboard.
- **Culling.** Drawing all 4096 tiles every frame would work, but it's wasteful. The camera
  can tell us which world rectangle is visible (`visibleBounds`), so we draw only the tiles
  inside it (a few hundred at the default zoom).

In **`MyGame.h`**, add the tile texture member:

```cpp
    gfx::Texture tileTexture_;
```

In **`MyGame.cpp`**, add the world constants and the texture generator inside the
anonymous namespace (below the step 3 constants):

```cpp
constexpr float kTileSize = 32.0f;
constexpr int kWorldTiles = 64;        // the world is 64 x 64 tiles
constexpr float kWorldSize = kTileSize * kWorldTiles;

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
```

In `onInit`, create the texture and start the player (and camera) in the middle of the
world:

```cpp
void MyGame::onInit(core::Application& app)
{
    gfx::VulkanContext& gpu = app.gpu();
    batch_ = std::make_unique<SpriteBatch>(gpu, app.renderer());

    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST; // keep pixels crisp when scaled up
    pixelArt.mipmaps = false;
    pixelArt.wrap = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    playerTexture_ = gfx::Texture::fromFile(gpu, core::assetPath("textures/player.png"), pixelArt);
    tileTexture_ = makeTileTexture(gpu);

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;

    // Start in the middle of the world, with the camera already there.
    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    camera_.position = playerPos_;
}
```

In `onFixedUpdate`, keep the player inside the world. Add this line right after
`playerPos_ += playerVel_ * dt;`:

```cpp
    playerPos_ = glm::clamp(playerPos_, glm::vec2(kPlayerSize * 0.5f), glm::vec2(kWorldSize - kPlayerSize * 0.5f));
```

In `onRender`, draw the tiles and a border **before** the player (draw order is layering;
later sprites appear on top). Insert this right after `batch_->begin(...)`:

```cpp
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
```

Things to notice:

- `tile.origin = (0, 0)` makes `position` the top-left corner, which is convenient for
  grids.
- All tiles share one texture, so the whole floor is drawn in **one draw call**, even
  though every tile has its own colour. `batch_->stats()` would confirm it.
- `drawRectOutline` (like `drawRect` and `drawLine`) draws with a built-in 1×1 white
  texture, so it needs no texture of its own.

**Run it:** a large checkerboard, and the slime can't leave it. But the camera stays put,
so the slime still walks off the screen.

---

## Step 5 — A camera that follows

Instead of snapping the camera to the player (harsh), move it a fraction of the way each
frame for a smooth follow. Follow the **interpolated** position, the same one used for
drawing, so the slime doesn't jitter relative to the camera.

`onUpdate` now uses `dt`, so name the parameter again, and add the zoom and follow code at
the end. The full function:

```cpp
void MyGame::onUpdate(core::Application& app, float dt)
{
    core::Input& in = app.input();

    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();

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
```

Things to notice:

- Zoom is multiplied by `exp(delta)` rather than adding to it, so zooming in and out feels
  equally fast at every zoom level.
- The follow speed `6.0` is a feel parameter: higher = tighter, lower = floatier.
- The camera lives in `onUpdate` (per frame), not in `onFixedUpdate`: camera movement
  should be as smooth as the display allows.

**Run it:** the view glides after the slime. Mouse wheel, **+/−**, or the gamepad
bumpers zoom between 0.25× and 4×. Zoom out to see the culling at work: tiles are only
drawn where the camera looks.

---

## Step 6 — Coins and particles

Time for gameplay: coins scattered around the world that burst into particles when
collected, and a manual particle burst on **Space** / gamepad **A**.

**Particles** are just a vector of small structs, updated in the fixed step and removed when
their life runs out. Each frame they're drawn as tinted, fading sprites. All particles and
coins share one round "orb" texture, so they batch into a single draw call.

Update **`MyGame.h`**. The finished header looks like this (new: `<random>`/`<vector>`,
the `Particle`/`Coin` structs, two helper functions, `orbTexture_` and the gameplay state):

```cpp
#pragma once

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"

#include <memory>
#include <random>
#include <vector>

class MyGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;

private:
    struct Particle {
        glm::vec2 position;
        glm::vec2 velocity;
        float life;
        float maxLife;
        render2d::Color color;
    };
    struct Coin {
        glm::vec2 position;
        float phase; // offsets the bobbing animation so coins don't move in sync
    };

    void spawnBurst(glm::vec2 at, int count, render2d::Color color);
    glm::vec2 randomWorldPoint();
    std::unique_ptr<render2d::SpriteBatch> batch_;
    render2d::Camera2D camera_;
    gfx::Texture playerTexture_;
    gfx::Texture tileTexture_;
    gfx::Texture orbTexture_;

    // Simulation state, advanced in onFixedUpdate. prevPlayerPos_ is the
    // position one step earlier, used to interpolate when rendering.
    glm::vec2 playerPos_{0.0f};
    glm::vec2 prevPlayerPos_{0.0f};
    glm::vec2 playerVel_{0.0f};
    glm::vec2 moveInput_{0.0f}; // written in onUpdate, read in onFixedUpdate
    bool facingLeft_ = false;

    std::vector<Particle> particles_;
    std::vector<Coin> coins_;
    int score_ = 0;
    std::mt19937 rng_{1234};
};
```

In **`MyGame.cpp`**, add `#include <glm/gtc/constants.hpp>` (for `two_pi`), then the coin
count and the orb texture generator in the anonymous namespace:

```cpp
constexpr int kCoinCount = 24;

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
```

Add the two helpers:

```cpp
glm::vec2 MyGame::randomWorldPoint()
{
    std::uniform_real_distribution<float> d(kTileSize * 2.0f, kWorldSize - kTileSize * 2.0f);
    return {d(rng_), d(rng_)};
}

void MyGame::spawnBurst(glm::vec2 at, int count, Color color)
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
```

In `onInit`, create the orb texture and place the coins:

```cpp
void MyGame::onInit(core::Application& app)
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

    // Show 360 world units vertically on any screen (720p, Deck 800p, 4K...).
    camera_.referenceHeight = 360.0f;

    // Start in the middle of the world, with the camera already there.
    playerPos_ = prevPlayerPos_ = glm::vec2(kWorldSize * 0.5f);
    camera_.position = playerPos_;

    for (int i = 0; i < kCoinCount; ++i)
        coins_.push_back({randomWorldPoint(), float(i)});
}
```

`onFixedUpdate` now needs the `app` parameter (for rumble). Add the `app` parameter:

```cpp
	void MyGame::onFixedUpdate(core::Application& app, float dt)
```

then add the coin and particle logic at the end of the function:

```cpp
    // --- Coins: collect when close enough ---
    for (Coin& c : coins_) {
        if (glm::distance(c.position, playerPos_) < 24.0f) {
            spawnBurst(c.position, 30, colors::Yellow);
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
```

In `onUpdate`, add the manual burst after the movement input:

```cpp
    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH))
        spawnBurst(playerPos_, 60, colors::Blue);
```

In `onRender`, draw coins and particles between the world border and the player:

```cpp
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
```

Things to notice:

- Collision here is just "distance below 24 units", which is plenty for a demo. Real games
  might use a spatial grid or a physics library such as Box2D.
- `rumble(low, high, ms)` gives a short vibration on gamepads (and the Deck) and does
  nothing without one.
- Alpha (`k`) fades particles out; the sprite pipeline uses alpha blending.
- The `std::mt19937` generator has a fixed seed, so the coin layout is the same every run.
  Seed it from the clock for variety.
- Coins bob using `app.time()` in `onRender`, a purely visual animation that doesn't need
  the fixed step.

**Run it:** walk into the yellow coins to collect them (with a gamepad, feel the rumble);
press **Space** for a blue burst.

---

## Step 7 — A HUD

The score display must not move or zoom with the world. The trick: a **second camera**
whose world units are exactly screen pixels (`referenceHeight = 0`, the default, and
positioned at the screen centre), used in a second `begin()/end()` pair after the world.

Add to **`MyGame.h`**:

```cpp
    render2d::Camera2D hudCamera_;
```

In `onRender`, after the world's `batch_->end();`, add:

```cpp
    // ===== HUD: a second camera where world units == screen pixels =====
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f;
    batch_->begin(frame, hudCamera_.viewProjection());

    const float ui = std::max(1.0f, viewport.y / 720.0f); // scale UI with resolution
    const int shown = std::min(score_, 40);
    for (int i = 0; i < shown; ++i)
        batch_->draw(orbTexture_, glm::vec2(24.0f * ui + float(i) * 18.0f * ui, 24.0f * ui), 0.22f * ui, 0.0f,
                     colors::Yellow);

    // Gamepad indicator: green when a gamepad (or the Deck's controls) is active.
    batch_->drawRect({viewport.x - 30.0f * ui, 14.0f * ui}, glm::vec2(16.0f * ui),
                     app.input().hasGamepad() ? colors::Green : Color(0.3f, 0.3f, 0.3f, 0.6f));

    batch_->end();
```

And as a small extra, toggle vsync with **V** in `onUpdate` (after the Esc check):

```cpp
    if (in.keyPressed(SDL_SCANCODE_V))
        app.renderer().setVsync(!app.renderer().vsync());
```

Things to notice:

- `SpriteBatch` supports several `begin()/end()` pairs per frame; each pair can use a
  different camera.
- The UI scale `ui` keeps the HUD readable on 1080p/4K screens while staying 1:1 at 720p.
  The Deck's 800p gets a factor of about 1.1.
- With vsync off, the title-bar FPS shows how fast the game really runs. The movement
  speed stays the same, thanks to the fixed timestep.

**Run it:** collected coins appear as a row of icons at the top left; a square at the top
right turns green when a gamepad is connected. **V** toggles vsync.

**That's the complete demo.** Your `MyGame` now behaves exactly like `DemoGame`. You can
delete `DemoGame.h/.cpp` from your own projects once you no longer need them for reference.

---

## Where to go next

Ideas, roughly in order of difficulty, each building on what you've used here:

1. **Sprite-sheet animation.** Put several frames in one PNG and pick one with
   `UvRect::fromPixels(texture, frame * 32, 0, 32, 32)`, advancing `frame` on a timer.
2. **Enemies.** A `std::vector<Enemy>` updated in `onFixedUpdate`, exactly like particles,
   chasing the player with `glm::normalize(player - enemy)`.
3. **Tilemaps from a file.** Load a map made in the Tiled editor (`.tmj` is JSON) and draw
   the visible tiles from a tileset atlas, just like step 4.
4. **Text.** Bake a font into an atlas with `stb_truetype` and draw each glyph as a sprite.
5. **Sound.** SDL3's audio streams, or a library such as miniaudio.
6. **Game states.** A title screen, pause menu and game-over screen: a small state enum in
   `MyGame`, switched on in `onUpdate`/`onRender`.
7. **Physics.** Box2D v3 for proper collisions and rigid bodies, stepped in
   `onFixedUpdate`.
