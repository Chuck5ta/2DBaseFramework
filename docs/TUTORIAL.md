# Game2D — Tutorial: Making the Demo

This tutorial rebuilds the demo game from an empty window, one feature at a time. It uses a
new class, `MyGame`, so the original `DemoGame` stays available for comparison. After
every step you can build and run the game and see the result.

**What you'll learn:** the `Game` hooks, drawing sprites, loading textures, keyboard and
gamepad input, the fixed timestep with interpolation, cameras, culling, simple particles,
sprite animation, a HUD, text, sound, audio effects, a debug UI, rebindable controls
(input actions), menu screens (game states) and saved settings and progress.

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
| [8](#step-8--text) | A font, HUD text and world-space text | Score, FPS, help panel, "+1" popups |
| [9](#step-9--sound) | Sound effects with `app.audio()` | A chime per coin, a whoosh per burst; **M** mutes |
| [10](#step-10--a-debug-ui) | A Dear ImGui window with `onDebugUI` | Live stats, sliders, buttons and debug drawing |
| [11](#step-11--sound-zones-reverb-and-muffling) | Audio effects: reverb and a low-pass filter | A cave that echoes and a pond that muffles |
| [12](#step-12--sprite-animation) | Sprite sheets, clips and an Animator | A hopping slime with dust on landing; spinning coins |
| [13](#step-13--input-actions-and-rebinding) | Named input actions with `app.actions()` | Help text that follows your device; rebinding in the debug UI |
| [14](#step-14--game-states-title-pause-and-game-over) | Game states with `app.states()` | A title screen, 60-second rounds, a pause menu and a "Time's up" screen |
| [15](#step-15--settings-and-save-files) | `core::SaveData` and `app.settings()` | An Options screen; the best score and your settings survive a restart |

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

---

## Step 8 — Text

Time to replace the icons with real text. A **`render2d::Font`** loads a TrueType/OpenType
file and bakes its characters into a texture (an *atlas*) with stb_truetype.
**`SpriteBatch::drawText`** then draws each character as a small textured quad, so text
batches with everything else and needs no special shader.

The project ships **Roboto Medium** in `assets/fonts/` (Apache 2.0 licence; see the README
there). Any `.ttf`/`.otf` works.

| `TextStyle` field | Meaning |
|---|---|
| `size` | Text size in world units (with the HUD camera: pixels) |
| `color` | sRGB colour; alpha fades text |
| `align` | `Left`, `Center` or `Right`, relative to the position you pass |
| `lineSpacing` | Multiplier for the gap between lines (`'\n'` starts a new line) |
| `snapToPixel` | Rounds positions to whole units. Use it with a 1:1 HUD camera for crisp text |

`drawText`'s position is the **top** of the first line. `font.measure(text, size)` returns
the size of a block of text before you draw it (handy for backgrounds and layout).

In **`MyGame.h`**, add the font after the HUD camera,

```cpp
    render2d::Font font_;
```

a `Popup` struct next to `Coin`,

```cpp
    struct Popup {
        glm::vec2 position;
        float life; // 1 = just spawned, 0 = gone
    };
```

and the new state at the end of the members:

```cpp
    std::vector<Popup> popups_;
    bool showHelp_ = true;
```

In **`MyGame.cpp`**, add `#include <cstdio>` and `#include <string>` to the includes. Load the
font in `onInit`, after the orb texture:

```cpp
    font_ = Font::load(gpu, core::assetPath("fonts/Roboto-Medium.ttf")); // baked at 48 px
```

In `onFixedUpdate`, spawn a popup when a coin is collected. Add this line right after
`spawnBurst(c.position, 30, colors::Yellow);`, *before* the coin moves:

```cpp
            popups_.push_back({c.position, 1.0f}); // "+1" floats up from the coin
```

and update the popups at the end of the function:

```cpp
    // --- "+1" popups: rise and fade over about a second ---
    for (Popup& p : popups_) {
        p.position.y -= 30.0f * dt;
        p.life -= dt;
    }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0.0f; }),
                  popups_.end());
```

In `onUpdate`, toggle the help panel with **H**, after the vsync toggle:

```cpp
    if (in.keyPressed(SDL_SCANCODE_H))
        showHelp_ = !showHelp_;
```

In `onRender`, draw the popups in the **world** layer, after the player (so they're on
top) and before the world's `batch_->end();`:

```cpp
    // --- "+1" popups: world-space text, drawn last so it's on top ---
    for (const Popup& p : popups_) {
        TextStyle style;
        style.size = 14.0f; // world units
        style.align = TextAlign::Center;
        style.color = Color(1.0f, 0.95f, 0.5f, std::min(p.life * 2.0f, 1.0f));
        batch_->drawText(font_, "+1", p.position - glm::vec2(0.0f, 34.0f), style);
    }
```

Finally, in the HUD part of `onRender`, replace the coin row and the gamepad square (keep
the `const float ui = ...` line) with:

```cpp
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
```

Things to notice:

- **Two kinds of text.** The "+1" popups are drawn with the *world* camera, so they move
  and zoom with the game (14 world units tall). The HUD uses the *screen* camera, so its
  size is in pixels.
- **Baked size vs drawn size.** The font is rasterised once at 48 px and scaled when drawn.
  Text is sharpest at or a little below the baked size. Bake larger (`FontDesc::pixelHeight`)
  if you draw big titles, and smaller for pixel-perfect small text.
- **Which characters.** By default printable ASCII and Latin-1 are baked (é, ü, ñ, £, ©…).
  Strings are UTF-8, so `"Café"` just works. Characters that weren't baked show as `?`.
- **Text panels** are just a `drawRect` behind the text, sized with `font.measure`.
- **`snprintf` into a small buffer** formats numbers without allocating every frame.

**Run it:** the score is a number next to a coin icon, the frame rate and input device are
shown at the top right, and a help panel sits at the bottom (**H** hides it). Collect a
coin and a "+1" floats up and fades away.

---

## Step 9 — Sound

The framework plays audio with **miniaudio**. `Application` owns an
**`audio::AudioEngine`**, reached with `app.audio()`. It opens the default output device
at startup and releases finished sounds every frame for you. If the PC has no audio
device, the game still runs: every audio call quietly does nothing.

The project ships two small sound effects in `assets/sounds/`: `coin.wav` and
`whoosh.wav` (CC0; see the README there). WAV, FLAC, MP3 and OGG Vorbis all work.

| `PlayParams` field | Meaning |
|---|---|
| `volume` | Linear gain; 1 = as recorded, 0.5 = half |
| `pitch` | Playback rate; 2 = an octave higher (and twice as fast) |
| `pan` | -1 = left, 0 = centre, +1 = right |
| `loop` | Plays until you call `app.audio().stop(handle)` |
| `spatial`, `position`, … | 3D sound relative to a listener (used in the Game3D tutorial) |

`play()` returns a **`SoundHandle`**. Keep it if you want to change or stop the sound
later; for one-shot effects you can ignore it.

In **`MyGame.h`**, add the sound file paths at the end of the members:

```cpp
    // Sound files (full paths, filled in onInit).
    std::string coinSound_;
    std::string whooshSound_;
```

and `#include <string>` to the includes.

In **`MyGame.cpp`**, load the sounds at the end of `onInit`:

```cpp
    // Sounds: decode them now so the first play has no delay.
    coinSound_ = core::assetPath("sounds/coin.wav");
    whooshSound_ = core::assetPath("sounds/whoosh.wav");
    app.audio().preload(coinSound_);
    app.audio().preload(whooshSound_);
```

In `onFixedUpdate`, play the chime when a coin is collected. Add this block right after
the `popups_.push_back(...)` line from step 8:

```cpp
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
```

In `onUpdate`, play the whoosh with the burst. The `if` now needs braces:

```cpp
    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH)) {
        spawnBurst(playerPos_, 60, colors::Blue);
        app.audio().play(whooshSound_, {.volume = 0.8f});
    }
```

and toggle mute with **M**, after the help toggle:

```cpp
    if (in.keyPressed(SDL_SCANCODE_M))
        app.audio().setMuted(!app.audio().muted());
```

In the HUD part of `onRender`, show when the game is muted, right after the
"Gamepad" / "Keyboard & mouse" line:

```cpp
    if (app.audio().muted()) {
        info.color = colors::Yellow;
        batch_->drawText(font_, "Muted", {viewport.x - 16.0f * ui, 56.0f * ui}, info);
    }
```

and add `M: mute` to the help text:

```cpp
        const char* help = "WASD / left stick: move      Space / (A): burst\n"
                           "Wheel / bumpers: zoom      M: mute      V: vsync      H: hide help";
```

Things to notice:

- **Preloading.** A sound is decoded the first time it plays and then cached in memory.
  `preload` does that work in `onInit`, so the first coin doesn't cause a small hitch.
- **Small variations go a long way.** A random pitch between 0.95 and 1.1 and a pan that
  follows the coin's position on screen stop repeated pickups from sounding mechanical.
- **Sounds in `onFixedUpdate`.** It's fine to call `play` from either update hook. It only
  queues the sound; mixing happens on miniaudio's own thread.
- **Volumes and mute.** `setMasterVolume`, `setSfxVolume` and `setMusicVolume` are ready
  for an options menu. `setMuted` silences everything without changing those values.
- **Too many sounds.** Up to 64 effects play at once (`AppConfig::audio.maxVoices`). After
  that, the oldest one-shot is cut, so a flood of pickups can't pile up.

**Music.** The demo has no music track, but it's one line once you add a file (OGG
Vorbis is a good choice: small and loops cleanly). Put it in `assets/music/` and call:

```cpp
app.audio().playMusic(core::assetPath("music/theme.ogg"), {.fadeInSeconds = 2.0f});
```

Music is streamed from disk rather than loaded whole, loops by default and is mixed on its
own volume (`setMusicVolume`, 0.8 by default). Calling `playMusic` again crossfades to the
new track. `stopMusic(seconds)`, `pauseMusic()` and `resumeMusic()` do what you'd expect.

**Run it:** collecting a coin plays a chime, from the left or right speaker depending on
where the coin was, and **Space** makes a whoosh. **M** mutes and "Muted" appears at the
top right.

---

## Step 10 — A debug UI

While you work on a game you constantly want to see a value, nudge a number or press a
"do it now" button without restarting. The framework includes
[Dear ImGui](https://github.com/ocornut/imgui) for exactly that.

Press **`** (the key below Esc) in any step of this tutorial and two things appear: the
framework's **Engine** window (frame time graph, GPU, vsync, audio volumes, input) and,
from the checkbox at its bottom, ImGui's own demo window, which shows every widget along
with the code that makes it. Press **`** again to hide them.

The debug UI exists in **Debug builds only** (`AppConfig::debugUI.enabled`), so players
never see it. When you ship a Release build it isn't even created.

ImGui is an *immediate-mode* UI: there are no widget objects to create or keep. Every
frame you call functions that both draw a widget and report what happened to it:

```cpp
if (ImGui::Button("Respawn"))           // true on the frame it's clicked
    respawn();
ImGui::SliderFloat("Speed", &speed_, 0.0f, 500.0f); // edits speed_ directly
```

You write that code in a new hook, **`Game::onDebugUI`**. It runs once per frame after
`onUpdate`, and only while the debug UI is visible. That's the only place to call ImGui.

In **`MyGame.h`**, declare the hook after `onRender`,

```cpp
    void onDebugUI(core::Application& app) override;
```

and add a flag for some debug drawing at the end of the members:

```cpp
    bool showPickupRange_ = false; // debug drawing, switched on in the debug UI
```

In **`MyGame.cpp`**, add `onDebugUI` after `onUpdate`:

```cpp
void MyGame::onDebugUI(core::Application& app)
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

    ImGui::Text("Score: %d", score_);
    ImGui::Text("Coins: %d   Particles: %d   Popups: %d", int(coins_.size()), int(particles_.size()),
                int(popups_.size()));

    ImGui::SeparatorText("Player");
    // Dragging the position teleports the player. Set the previous position too,
    // or interpolation would show it sliding there.
    if (ImGui::DragFloat2("Position", &playerPos_.x, 1.0f, 0.0f, kWorldSize, "%.0f"))
        prevPlayerPos_ = playerPos_;
    ImGui::Text("Velocity: %.0f, %.0f", double(playerVel_.x), double(playerVel_.y));

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
        ImGui::Text("Font atlas (%u x %u):", font_.texture().width(), font_.texture().height());
        ImGui::Image(ui.texture(font_.texture()), ImVec2(256.0f * scale, 256.0f * scale));
    }
    ImGui::End();
}
```

In `onRender`, draw the pickup range when it's switched on. Add this right after the
player is drawn:

```cpp
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
```

Finally, add the key to the help text:

```cpp
        const char* help = "WASD / left stick: move      Space / (A): burst      Wheel / bumpers: zoom\n"
                           "M: mute      V: vsync      H: hide help      ` (below Esc): debug UI";
```

Things to notice:

- **Widgets edit your variables directly.** `SliderFloat("Zoom", &camera_.zoom, ...)` reads
  and writes `camera_.zoom`; there's no copying back and forth. Functions such as
  `DragFloat2` return `true` when the value changed, for when you need to react (here,
  keeping the interpolation from sliding).
- **`Begin`/`End` make a window.** Always call `End`, even when `Begin` returns `false` (the
  window is collapsed). `SetNextWindowPos` with `ImGuiCond_FirstUseEver` sets where it
  starts; after that you can drag it anywhere.
- **Labels are IDs.** Two widgets with the same label in one window clash. Add a hidden
  suffix to tell them apart: `"Speed##enemy"` shows as "Speed".
- **Clicks don't leak into the game.** While the mouse is over a debug window, or you are
  typing into a text field, those events don't reach `app.input()`. (Try zooming with the
  wheel over a window.)
- **Debug drawing** is ordinary drawing behind a flag. Here it shows the actual pickup test:
  the green circle is `24` units around the player, and the red dots are where coins really
  are. The coin sprites bob up and down, but the collision position doesn't move.
- **Textures in the UI.** `app.debugUI()->texture(t)` turns any `gfx::Texture` into
  something `ImGui::Image` can show, including the font atlas from step 8.
- **ImGui's colours are sRGB.** The framework's ImGui shader converts them for our sRGB
  swapchain, so colours you pick in ImGui look the same as in any image editor.

**Run it:** press **`**. A "Game" window at the top right shows the score and particle
count, and lets you drag the player around, zoom, spawn bursts, scatter the coins and show
the pickup range. Open **Textures** to see the game's textures and the font atlas.

---

## Step 11 — Sound zones: reverb and muffling

Effects make a place sound like a place. The audio engine has four:

| Effect | What it does | Typical use |
|---|---|---|
| **Low-pass** | Lets only the low frequencies through ("muffles") | Under water, behind a wall, a pause menu, being stunned |
| **High-pass** | Lets only the high frequencies through ("thin") | A radio, a phone, a tiny speaker |
| **Echo** | Distinct repeats after a delay | A canyon, a long corridor |
| **Reverb** | A room's reflections blending into a tail | A cave, a hall, a bathroom |

Low-pass, high-pass and echo work on a **bus**: `audio::Bus::Sfx` (all sound effects),
`Bus::Music` or `Bus::Master` (everything). The **reverb** is shared: every sound effect
sends some of itself to it, and music never does. Each sound can also have its **own**
low-pass, for one sound behind a wall (the Game3D tutorial uses that).

Every change **glides** to the new value (0.3 seconds by default, or the time you pass),
because a filter that snaps on sounds like a glitch.

This step adds two zones next to the start: a **cave** that turns on a cave reverb, and a
**pond** that muffles the sound effects while you're in the water.

In **`MyGame.h`**, add the zone state at the end of the members:

```cpp
    // Which sound zone the player is in (effects change when this changes).
    bool inCave_ = false;
    bool inPond_ = false;
```

In **`MyGame.cpp`**, describe the zones with the other constants, after `kCoinCount`:

```cpp
// Sound zones near the start: reverb inside the cave, muffled sound in the pond.
struct Zone {
    glm::vec2 min, max; // world units
    bool contains(glm::vec2 p) const { return p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y; }
};
constexpr Zone kCave{{20 * kTileSize, 26 * kTileSize}, {28 * kTileSize, 38 * kTileSize}};
constexpr Zone kPond{{36 * kTileSize, 28 * kTileSize}, {45 * kTileSize, 36 * kTileSize}};
```

At the end of `onUpdate`, after the camera code, change the effects when the player enters
or leaves a zone:

```cpp
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
```

In `onRender`, draw the zones after the world border (so coins and the player are drawn
on top of them):

```cpp
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
```

Finally, in `onDebugUI`, show the zone and add two shortcuts, after the "Velocity" line:

```cpp
    ImGui::Text("Sound zone: %s", inCave_ ? "cave (reverb)" : inPond_ ? "pond (muffled)" : "none");
    if (ImGui::Button("Go to the cave"))
        playerPos_ = prevPlayerPos_ = (kCave.min + kCave.max) * 0.5f;
    ImGui::SameLine();
    if (ImGui::Button("Go to the pond"))
        playerPos_ = prevPlayerPos_ = (kPond.min + kPond.max) * 0.5f;
```

Things to notice:

- **Change effects on transitions, not every frame.** The code remembers which zone the
  player was in and calls the audio engine only when that changes. Calling `setReverb`
  every frame would work, but would restart the glide each time.
- **Presets are a starting point.** `ReverbParams::preset(ReverbPreset::Cave)` returns
  plain numbers (`level`, `roomSize`, `damping`, `width`), so you can tweak them:
  `auto cave = ReverbParams::preset(ReverbPreset::Cave); cave.level = 0.8f;`.
- **Only sound effects are muffled under water.** The low-pass is on `Bus::Sfx`. Music
  would stay clear; use `Bus::Master` to muffle everything (a pause menu, say).
- **The reverb tail outlives the sound.** A coin chime is short, but in the cave its
  reverb keeps ringing for a couple of seconds. Leaving the cave fades it out.
- **Keeping a sound dry.** Interface sounds such as menu clicks shouldn't echo in a cave:
  play them with `{.reverbSend = 0.0f}`.
- **Try every effect live.** In the debug UI, open **Engine → Audio → Audio effects** for
  sliders on each bus, reverb presets and a reset button. When you find values you like,
  copy them into your code.

**Run it:** walk left into the dark **CAVE** and press **Space**: the whoosh rings on in
the cave. Collect a coin there and the chime rings on too. Walk right into the **POND**: the
same sounds become dull and muffled, and they're clear again as soon as you step out.

---

## Step 12 — Sprite animation

Until now the slime has been one picture squashed in code, and the coins a tinted circle.
This step gives them real animation: a sequence of drawings (**frames**) in one image (a
**sprite sheet**), grouped into named **clips** such as "idle" or "hop".

The framework's `render2d/Animation.h` has three pieces:

| Piece | Job |
|---|---|
| **`SpriteSheet`** | The texture, where each frame is in it, and the named clips |
| **`AnimationClip`** | A list of frames with their durations, how it repeats (`Loop`, `Once`, `PingPong`) and optional named **events** |
| **`Animator`** | Plays one clip for one object: call `play`, `update`, then draw with `uv()` |

A sheet can be made in two ways:

- **From [Aseprite](https://www.aseprite.org/)**, the most popular pixel-art editor. *File →
  Export Sprite Sheet* with *JSON Data* writes a PNG and a JSON file. `loadAseprite` reads
  both: each **tag** becomes a clip, and each frame keeps the duration you gave it. A tag
  set to *Ping-pong* ping-pongs, *Reverse* plays backwards, and *Repeat 1* plays once.
- **From a plain grid** of equal frames (`loadGrid`), adding clips in code.

The project ships both kinds in `assets/sprites/` (CC0, see the README there):
`slime.png` + `slime.json` (Aseprite format, tags **idle**, **hop** and **burst**) and
`coin.png` (8 frames of 16×16 in a row).

In **`MyGame.h`**, add `#include "render2d/Animation.h"` with the other includes, and at the
end of the members:

```cpp
    // Animation: the player's clips come from an Aseprite export; the coins are a grid.
    render2d::SpriteSheet slimeSheet_;
    render2d::SpriteSheet coinSheet_;
    render2d::Animator playerAnim_;
    bool bursting_ = false; // the "burst" clip is playing; don't switch to idle/hop yet
```

In **`MyGame.cpp`**, load the sheets in `onInit`, right after the player texture:

```cpp
    // Animated sprites. The slime sheet is an Aseprite export: its tags (idle, hop,
    // burst) become clips. The coin is a plain row of 16x16 frames.
    slimeSheet_ = SpriteSheet::loadAseprite(gpu, core::assetPath("sprites/slime.json"));
    slimeSheet_.clip("hop")->addEvent(5, "land"); // frame 5: the slime hits the ground
    coinSheet_ = SpriteSheet::loadGrid(gpu, core::assetPath("sprites/coin.png"), {16, 16});
    playerAnim_.play(slimeSheet_, "idle");
```

At the end of `onFixedUpdate`, choose the player's clip and advance it:

```cpp
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
```

In `onUpdate`, **Space** now also plays the burst clip. Replace the burst code:

```cpp
    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH)) {
        spawnBurst(playerPos_, 60, colors::Blue);
        app.audio().play(whooshSound_, {.volume = 0.8f});
        playerAnim_.play(slimeSheet_, "burst", true); // restart, even if already bursting
        bursting_ = true;
    }
```

In `onRender`, the player's sprite uses the current frame. Replace everything from
`player.uv = ...` down to `batch_->draw(playerTexture_, player);` (the squash and stretch
done in code isn't needed any more) with:

```cpp
    // The current frame of the player's animation (the art faces right).
    player.uv = playerAnim_.uv(facingLeft_);
    batch_->draw(slimeSheet_.texture(), player);
```

Replace the coin loop with spinning coins:

```cpp
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
```

And in `onDebugUI`, after the sound-zone buttons:

```cpp
    ImGui::Text("Animation: %s, frame %d", playerAnim_.clipName().c_str(), playerAnim_.frame());
    ImGui::SliderFloat("Animation speed", &playerAnim_.speed, 0.0f, 3.0f, "%.2fx");
```

Things to notice:

- **`play()` every step is fine.** Asking for the clip that's already playing does nothing,
  so the code simply says what *should* be playing ("hop" while moving, "idle" otherwise).
  Pass `true` as the third argument to restart a clip, as the burst does.
- **Once clips and `finished()`.** The burst is a *Repeat 1* tag, so it plays once and holds
  its last frame. `finished()` tells the game when to hand back to idle or hop.
- **Events tie gameplay to the art.** `addEvent(5, "land")` names the frame where the slime
  touches the ground, and `eventFired("land")` is true on the step that frame starts. The
  same idea will say "the punch connects now" in a fighting game. If a long step skips
  frames, their events still fire.
- **Update animation in `onFixedUpdate`** when it drives gameplay (events, hit frames), so
  it runs at the same rate as the simulation. Each frame changes on exactly the right tick.
  Purely decorative animation can be updated in `onUpdate` instead.
- **Facing left.** Draw the art facing right; `uv(true)` mirrors it.
- **Not everything needs an Animator.** Two dozen identical coins just pick a frame from the
  clock (`coinSheet_.frame(i).uv`), offset per coin so they don't spin in step.
- **Crisp frames.** Sheets load with `SpriteSheet::pixelArt()`: nearest filtering and no
  mipmaps. Mipmaps would blend neighbouring frames together. For smooth (non-pixel) art,
  pass a `TextureDesc` with linear filtering and leave a pixel or two of space between frames.
- **Exporting from Aseprite:** *Sheet type* "By Rows" (or any), *JSON Data* ticked as
  *Array* (Hash works too), *Tags* ticked, *Trim* **unticked** (trimmed frames would jump
  around). Put the PNG next to the JSON in `assets/`.

**Run it:** the slime breathes gently while standing still, hops when you move (with a
puff of dust each time it lands), and puffs up happily when you press **Space**. The coins
spin. In the debug UI, the Game window shows the clip and frame. Slow the animation down
to see each frame, and open **Textures** to see both sheets.

---

## Step 13 — Input actions and rebinding

So far the game asks about physical keys: "is **D** or **Right** or the d-pad held?". That
works, but every place that reads input repeats the list, the help text has to be kept in
step by hand, and players can't change their controls (which many expect, and which
matters for accessibility).

**Input actions** fix this. The game names what the player *does* ("left", "burst") and
lists which inputs trigger each one, once. Everything else asks about the name:

| Before | After |
|---|---|
| `in.keyPressed(SDL_SCANCODE_SPACE) \|\| in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH)` | `actions.pressed("burst")` |
| eight `keyDown`/`gamepadDown` lines plus the stick | `actions.vector("left", "right", "up", "down")` |
| `"Space / (A): burst"` typed into the help text | `actions.prompt("burst")` → `"Space"` or `"A"` |

`app.actions()` (`core/Actions.h`) is updated by the framework every frame, right after
input, so it is ready in `onUpdate`. A **binding** is one physical input:
`Binding::key(...)`, `Binding::mouse(...)`, `Binding::button(...)` for a gamepad button,
or `Binding::axis(axis, -1 or +1)` for one direction of a stick, or a trigger.

In **`MyGame.h`**, at the end of the members:

```cpp
    // Rebinding in the debug UI: the action waiting for a new input ("" = none), and
    // a line about the last change.
    std::string rebinding_;
    std::string rebindNote_;
```

In **`MyGame.cpp`**, add `#include <optional>` with the other includes. Then define the
actions at the end of `onInit`:

```cpp
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
    actions.define("quit", {Binding::key(SDL_SCANCODE_ESCAPE), Binding::button(SDL_GAMEPAD_BUTTON_BACK)});
```

In `onUpdate`, replace everything from `core::Input& in = app.input();` down to the mute
key with:

```cpp
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

    if (actions.pressed("quit"))
        app.quit();
    if (actions.pressed("vsync"))
        app.renderer().setVsync(!app.renderer().vsync());
    if (actions.pressed("help"))
        showHelp_ = !showHelp_;
    if (actions.pressed("mute"))
        app.audio().setMuted(!app.audio().muted());
```

Replace the movement code (`glm::vec2 move(0.0f);` down to `moveInput_ = move;`):

```cpp
    // --- Movement: four actions make one direction (analog with a stick) ---
    moveInput_ = actions.vector("left", "right", "up", "down");
```

In the burst code, change the `if` line to `if (actions.pressed("burst")) {`. In the zoom
code, replace the five `zoomDelta` lines with:

```cpp
    // (The wheel isn't an action: it has no "held" state, just an amount per frame.)
    const float zoomDelta = in.mouseWheel() * 0.1f + (actions.value("zoom in") - actions.value("zoom out")) * 1.5f * dt;
```

The help panel builds its text from the bindings. Add these two functions inside the
`namespace { }` near the top of the file:

```cpp
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
           actions.prompt("mute") + ": mute      " + actions.prompt("vsync") + ": vsync      " +
           actions.prompt("help") + ": hide help      ` (below Esc): debug UI";
}
```

In `onRender`, replace the `const char* help = ...;` lines with:

```cpp
        const std::string help = helpText(app.actions());
```

and change the HUD's device line to show the device used *last*, rather than whether a
pad is plugged in:

```cpp
    const bool pad = app.actions().lastDevice() == core::InputDevice::Gamepad; // the one used last
```

Finally, a rebinding window. In `onDebugUI`, replace the final `ImGui::End();` (after the
Textures section) with:

```cpp
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
```

Things to notice:

- **Movement is one line.** `vector()` combines four actions into a direction: diagonals
  aren't faster, and a half-tilted stick walks at half speed, as before.
- **`pressed` never misses a tap.** A key pressed and released between two frames still
  counts as one press.
- **Prompts follow the player.** Press a gamepad button and the help panel switches to
  "A: burst"; touch the keyboard and it switches back. The button names match the pad:
  A/B/X/Y on Xbox pads and the Steam Deck, Cross/Circle/Square/Triangle on PlayStation.
- **Rebinding is a few calls.** `capture()` returns the first input pressed this frame,
  `addBinding`/`removeBinding` change an action, `resetAllToDefaults` undoes everything,
  and `actionsUsing` finds clashes. Two actions *may* share an input (Space could burst and
  confirm a menu), so the demo only mentions it.
- **While waiting for a new input, the game ignores its controls**, so the key you press
  doesn't also do its old job. **Esc** cancels.
- **Saving.** "Copy as JSON" puts `saveBindings()` on the clipboard:
  `{"burst": ["key:Space", "pad:a"], ...}`. A settings file can store that text and pass it to
  `loadBindings` at start-up. (Settings files are a later feature of the framework.)
- **Fixed-step games: use `consume`.** `pressed` is true for one *frame*, but
  `onFixedUpdate` can run zero or several times per frame. For a punch or jump read in
  `onFixedUpdate`, use `actions.consume("punch")`: the press is kept for a moment (0.15 s
  by default) until a step uses it, so it's never lost or used twice. This demo reads its
  buttons in `onUpdate`, so it doesn't need it.
- **The raw input is still there.** `app.input()` is still the right tool for things that
  aren't really actions: the mouse wheel, the cursor position, or a level editor's
  hot keys.

**Run it:** everything plays as before. Plug in a gamepad (or press a button on it) and
the help panel shows its buttons. Open the debug UI (**`**) and click the **Controls**
title at the top: click **+** next to "burst" and press **J**. Now **J** bursts too.
Click a binding to remove it, and **Reset all to defaults** to start again.

---

## Step 14 — Game states: title, pause and game over

A real game is more than its gameplay: it starts on a title screen, pauses, and ends with
a result. Each of those is a **game state**: a screen with its own update and drawing that
sits on top of the game.

`app.states()` (`core/GameState.h`) is a **stack** of them, above your `Game`:

```
  Pause         <- top: updated and drawn last
  ...
  your Game     <- always the bottom layer
```

A state is a class derived from `core::GameState`, with the same hooks as the game
(`onUpdate`, `onFixedUpdate`, `onRender`...) plus `onEnter`/`onExit` (added to and removed
from the stack) and `onPause`/`onResume`. Two settings decide how it affects what's below:

| Setting | Default | Meaning |
|---|---|---|
| `pausesBelow` | `true` | Layers below get no `onFixedUpdate`/`onUpdate`: the game freezes |
| `hidesBelow` | `false` | Layers below aren't drawn (for a full-screen, opaque menu) |

So a pause menu is just a state pushed on top: the game stops, still visible underneath,
and carries on when the state is popped. `push`, `pop`, `replace` (swap the top state for
another) and `clear` take effect **at the start of the next frame**, so a state can remove
itself from its own `onUpdate` safely.

This step makes the demo a little game: rounds of 60 seconds, a title screen, a pause
menu, and a "Time's up" screen with the best score.

### The game's side

In **`MyGame.h`**, after `onDebugUI`'s declaration:

```cpp
    void onPause(core::Application& app) override; // a menu opened over the game

    // Used by the menu screens (game states) in MyGame.cpp.
    void startRound();
    int bestScore() const { return bestScore_; }
```

and at the end of the members:

```cpp
    // Rounds: seconds left (0 = no round running), and the best score so far.
    float roundTime_ = 0.0f;
    int bestScore_ = 0;
    friend class MenuState; // the menu screens draw with our SpriteBatch, font and HUD camera
```

`onPause` is a `core::Game` hook too: it runs when a state that pauses the game is pushed.
(`friend class MenuState` lets the menu screens draw with the game's `SpriteBatch`, font and
HUD camera, rather than loading their own.)

### A menu

In **`MyGame.cpp`**, add this inside the `namespace { }` (after `helpText`). It's a list of
choices that works with the keys, a gamepad, and the mouse:

```cpp
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
```

### The three screens

Add these right after the `namespace { }`, before `onInit`. They share a base class that
draws the dimmed game, a heading, a line of text and the menu:

```cpp
// --- Game states: the menu screens ---------------------------------------------------
// Each is a core::GameState pushed onto app.states(). They pause the game underneath
// (pausesBelow, the default) but let it show through, dimmed.

// What the three screens share: the dimmed game, a heading, a line of text, a menu.
class MenuState : public core::GameState {
protected:
    MenuState(MyGame& game, std::vector<std::string> items) : game_(game), menu_(std::move(items)) {}

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

    MyGame& game_;
    Menu menu_;
};

class TitleState : public MenuState {
public:
    explicit TitleState(MyGame& game) : MenuState(game, {"Play", "Quit"}) { name = "Title"; }

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
    explicit PauseState(MyGame& game) : MenuState(game, {"Resume", "Restart", "Quit to title"}) { name = "Pause"; }

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
    RoundOverState(MyGame& game, int score, bool newBest)
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
```

### Starting, pausing and ending

In `onInit`, replace the `"quit"` action with three for the menus:

```cpp
    actions.define("pause", {Binding::key(SDL_SCANCODE_ESCAPE), Binding::button(SDL_GAMEPAD_BUTTON_START)});
    actions.define("confirm", {Binding::key(SDL_SCANCODE_RETURN), Binding::key(SDL_SCANCODE_SPACE),
                               Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)});
    actions.define("back", {Binding::key(SDL_SCANCODE_BACKSPACE), Binding::button(SDL_GAMEPAD_BUTTON_EAST)});
```

and at the very end of `onInit`:

```cpp
    // Start on the title screen, over the (paused) game.
    app.states().push<TitleState>(*this);
```

After `onShutdown`, add the round reset and the pause hook:

```cpp
// A fresh round: score, clock, player and coins back to the start.
void MyGame::startRound()
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
void MyGame::onPause(core::Application&)
{
    prevPlayerPos_ = playerPos_;
}
```

At the start of `onFixedUpdate`, the round's clock:

```cpp
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
```

In `onUpdate`, Esc no longer quits (that's on the title screen now). Replace the `"quit"`
lines with:

```cpp
    if (actions.pressed("pause"))
        app.states().push<PauseState>(*this); // on top from the next frame; the game stops
```

### The HUD

In `onRender`, after the score, draw the clock:

```cpp
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
```

Change the help panel's `if (showHelp_) {` to
`if (showHelp_ && !app.states().gamePaused()) {` (the menus show their own help), and in
`helpText`, start the second line with the pause button:

```cpp
           actions.prompt("pause") + ": pause      " +
           actions.prompt("mute") + ": mute      " + ...
```

For testing, in `onDebugUI`, show the round in the first line and add a button after
"Reset score":

```cpp
    ImGui::Text("Score: %d   Best: %d   Time left: %.1f s", score_, bestScore_, double(roundTime_));
```

```cpp
    ImGui::SameLine();
    if (ImGui::Button("End round")) // quicker than waiting a minute to test the result screen
        roundTime_ = std::min(roundTime_, 0.01f);
```

Things to notice:

- **The game didn't need to know about menus.** Its `onUpdate` and `onFixedUpdate` simply
  aren't called while a menu is up, so the player, coins, particles and clock all freeze
  by themselves. Only `onPause` was needed, for the interpolation (see its comment).
- **Pausing is one line**, `app.states().push<PauseState>(*this)`. Resuming is the pause
  screen popping itself.
- **`replace`** swaps the top state: "Quit to title" replaces the pause screen with the
  title, in one step, without the game resuming in between.
- **`timeInState()`** is the seconds since the state was entered (while active). The menus
  fade in with it, and the "Time's up" screen ignores input for 0.6 s with it, so a button
  being mashed when time runs out doesn't skip the result.
- **`onEnter`/`onExit`** suit things that start and stop with a screen: the pause menu
  muffles all sound with a low-pass on the master bus (step 11's effects) and restores it.
- **A press is used once.** Whenever the stack changes, the framework forgets that
  frame's presses (`Actions::clearPresses`), so the A that picked "Resume" doesn't also
  burst, and a buffered `consume` won't fire afterwards.
- **The Engine panel shows the stack.** Open the debug UI and expand **Game states** to see
  each state, top first, and what's paused.
- The best score is kept only while the game runs. Step 15 saves it.

**Run it:** the game starts on the title. **Play** starts a round: collect coins until the
clock runs out. **Esc** (or **Start** on a pad) pauses; the sound goes muffled. **Back**
(or **B**) also resumes. Try the mouse on the menus too. In the debug UI, **End round**
skips to the result.

---

## Step 15 — Settings and save files

Close the game and everything is forgotten: the best score, the volume, the controls you
rebound. Games keep two kinds of things between sessions:

- **Settings**: how the player likes the game (volume, fullscreen, controls).
- **Progress**: what they've achieved (scores, unlocked levels, save slots).

Both go in files in the **player's own save folder**, never next to the game's `.exe`
(Steam installs games in a folder players may not be allowed to write to):

| System | Save folder |
|---|---|
| Windows | `%APPDATA%\MyStudio\Game2D\` (for example `C:\Users\you\AppData\Roaming\MyStudio\Game2D\`) |
| Linux, Steam Deck | `~/.local/share/MyStudio/Game2D/` |

The names come from `main.cpp`: `config.organization` (your studio, or your own name) and
`config.application`. Open the debug UI's Engine panel and expand **Settings & saves** to
see the folder; **Open folder** shows it in Explorer.

### Settings: already done for you

The framework keeps **`settings.json`** by itself. At start it loads the file and starts
the game the way the player left it, and at exit it saves:

```json
{
  "audio": { "master": 0.7, "music": 0.8, "muted": false, "sfx": 1.0 },
  "controls": { "burst": ["key:J", "pad:a"] },
  "game": { "showHelp": false },
  "version": 1,
  "video": { "fullscreen": false, "vsync": false }
}
```

So since step 13, any controls you rebind in the debug UI are remembered, and so are
**M** (mute), **V** (vsync) and **F11** (fullscreen). Only changed controls are saved, so if
you change a default in code, everyone who didn't rebind it gets the new one.

`app.settings()` is that file, so your game can keep its own settings in it too, under
`"game"`. `app.saveSettings()` writes it straight away, and `app.resetSettings()` puts
everything back to the defaults.

### Progress: a `core::SaveData`

`core::SaveData` (`core/SaveData.h`) is a JSON file of named values:

```cpp
core::SaveData save("save.json");      // in the save folder
save.load();                           // false the first time: there's no file yet
int best = save.getInt("bestScore");   // 0 if it isn't there (or getInt("bestScore", 10))
save.set("stats.rounds", 3);           // dots make groups: { "stats": { "rounds": 3 } }
save.save();                           // writes the file
```

There are `getBool`/`getInt`/`getFloat`/`getString` and `set` for each type, plus `has`,
`remove`, `erase` (delete the file: "new game") and `json()` for lists. Saving is
**crash-safe**: the new data is written to a temporary file that then replaces the old
one, and the previous version is kept as `save.json.bak`. If the file is ever damaged (a
power cut at the wrong moment), `load()` uses the backup.

In **`MyGame.h`**, after `bestScore()`:

```cpp
    const core::SaveData& saveData() const { return save_; }
```

and at the end of the members:

```cpp
    // Progress kept between sessions: save.json in the player's save folder.
    core::SaveData save_{"save.json"};
```

In **`MyGame.cpp`**, in `onInit`, just before the title screen is pushed:

```cpp
    // Progress from earlier sessions. The first time there's no file: every get then
    // returns its fallback (0 here).
    save_.load();
    bestScore_ = save_.getInt("bestScore");
    // A setting of the game's own, kept with the framework's in settings.json.
    showHelp_ = app.settings().getBool("game.showHelp", true);
```

In `onFixedUpdate`, when the round ends, save after updating `bestScore_`:

```cpp
            // Keep the best score and some lifetime stats in save.json.
            save_.set("bestScore", bestScore_);
            save_.set("stats.rounds", save_.getInt("stats.rounds") + 1);
            save_.set("stats.coins", save_.getInt("stats.coins") + score_);
            save_.save();
```

In `onUpdate`, remember the help toggle as a setting:

```cpp
    if (actions.pressed("help")) {
        showHelp_ = !showHelp_;
        app.settings().set("game.showHelp", showHelp_); // saved with the other settings at exit
    }
```

And in `onDebugUI`, after the "End round" button, a way to start again from scratch:

```cpp
    ImGui::SameLine();
    if (ImGui::Button("Delete save")) { // back to a first run: no best score, no stats
        save_.erase();
        bestScore_ = 0;
    }
```

### An Options screen

Give `Menu` two more functions, so a screen can change an item's text and see which item
is highlighted. Add them just before its `private:`:

```cpp
    int selected() const { return selected_; }
    void setItem(int index, std::string text) { items_[size_t(index)] = std::move(text); } // e.g. "Volume: 80%"
```

With one menu on top of another (Options over the pause screen), only the top one should
draw. At the start of `MenuState::drawScreen`:

```cpp
        if (app.states().top() != this)
            return; // another menu (Options) is on top: let it have the screen
```

The Options screen goes **before** `TitleState`, since the title and pause screens open
it:

```cpp
// Options: volume, fullscreen and vsync. It goes on top of the title or pause screen,
// which step aside while it's there (see drawScreen). The framework keeps these in the
// player's settings.json by itself; saveSettings() just writes them straight away.
class OptionsState : public MenuState {
public:
    explicit OptionsState(MyGame& game) : MenuState(game, {"Volume", "Fullscreen", "VSync", "Back"}) { name = "Options"; }

    void onUpdate(core::Application& app, float) override
    {
        const core::Actions& actions = app.actions();
        const int chosen = menu_.update(app);
        if (menu_.selected() == 0) {
            // Volume in steps of 10%: left/right, or choosing it goes up and wraps round.
            int step = int(std::lround(app.audio().masterVolume() * 10.0f));
            if (actions.pressed("left"))
                step = std::max(step - 1, 0);
            if (actions.pressed("right"))
                step = std::min(step + 1, 10);
            if (chosen == 0)
                step = (step + 1) % 11;
            app.audio().setMasterVolume(float(step) / 10.0f);
        }
        if (chosen == 1)
            app.window().toggleFullscreen();
        if (chosen == 2)
            app.renderer().setVsync(!app.renderer().vsync());
        if (chosen == 3 || actions.pressed("back") || actions.pressed("pause")) {
            app.saveSettings(); // now, rather than only at exit
            app.states().pop();
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        const auto onOff = [](bool on) { return std::string(on ? "On" : "Off"); };
        menu_.setItem(0, "Volume: " + std::to_string(int(std::lround(app.audio().masterVolume() * 100.0f))) + "%");
        menu_.setItem(1, "Fullscreen: " + onOff(app.window().isFullscreen()));
        menu_.setItem(2, "VSync: " + onOff(app.renderer().vsync()));
        const core::Actions& actions = app.actions();
        drawScreen(app, frame, "Options", actions.prompt("left") + "/" + actions.prompt("right") + ": volume");
    }
};
```

The title screen gets an **Options** item, and shows the saved best score and stats:

```cpp
class TitleState : public MenuState {
public:
    explicit TitleState(MyGame& game) : MenuState(game, {"Play", "Options", "Quit"}) { name = "Title"; }

    void onUpdate(core::Application& app, float) override
    {
        switch (menu_.update(app)) {
        case 0: // Play
            game_.startRound();
            app.states().pop(); // the game carries on underneath (next frame)
            break;
        case 1:
            app.states().push<OptionsState>(game_);
            break;
        case 2:
            app.quit();
            break;
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        // The best score and stats come from save.json, so they survive closing the game.
        const core::SaveData& save = game_.saveData();
        drawScreen(app, frame, "Slime Garden",
                   save.has("bestScore")
                       ? "Best score: " + std::to_string(save.getInt("bestScore")) + "   Coins, all time: " +
                             std::to_string(save.getInt("stats.coins"))
                       : "Collect all the coins you can in " + std::to_string(int(kRoundSeconds)) + " seconds");
    }
};
```

and the pause screen gets one too (note the cases after it move down by one):

```cpp
class PauseState : public MenuState {
public:
    explicit PauseState(MyGame& game) : MenuState(game, {"Resume", "Restart", "Options", "Quit to title"}) { name = "Pause"; }

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
            app.states().push<OptionsState>(game_); // on top; this screen stays underneath
            break;
        case 3:
            app.states().replace<TitleState>(game_); // swap this screen for the title
            break;
        }
    }
    void onRender(core::Application& app, gfx::FrameContext& frame) override
    {
        drawScreen(app, frame, "Paused", "");
    }
};
```

Things to notice:

- **The game didn't load or save any settings itself.** Volume, fullscreen and vsync are
  just changed on the audio engine, window and renderer; the framework saves what they
  are at exit. `saveSettings()` on leaving Options writes them immediately, in case the
  game crashes or is killed later.
- **Options is pushed on top of Pause**, not swapped in, so **Back** (or Esc, or B)
  returns to the pause menu. Pause stays muffled because its `onExit` hasn't run.
- **Progress is saved when something happens** (a round ends), not at exit: a player who
  quits with Alt+F4, or whose battery dies, keeps their score.
- **Save slots** are just more files: `core::SaveData slot("slot" + std::to_string(n) + ".json")`.
- **Steam Cloud** can sync the save folder without any code: in Steamworks' Steam Cloud
  (Auto-Cloud) settings, add a root path of `WinAppDataRoaming` with the subdirectory
  `MyStudio/Game2D`, and a root override for Linux to `LinuxXdgDataHome` (the Steam Deck
  is Linux). Check Steamworks' own Steam Cloud page when you set it up.
- To start completely fresh while testing, use **Delete save** (the Game window) and
  **Reset settings** (the Engine panel's "Settings & saves"), or delete the folder while
  the game isn't running.

**Run it:** play a round, then close the game and start it again: the title shows your
best score and coins collected. Open **Options** from the title or the pause menu: change
the volume with left/right, switch fullscreen or vsync. Close and restart: they're
remembered, and so is the help panel (**H**) and any controls you rebound.

**That's the complete demo.** Your `MyGame` now behaves exactly like `DemoGame`. You can
delete `DemoGame.h/.cpp` from your own projects once you no longer need them for reference.

---

## Where to go next

Ideas, roughly in order of difficulty, each building on what you've used here:

1. **More animation.** Draw your own character in Aseprite with idle, walk and attack
   tags, and play the attack clip from a `consume("attack")` in `onFixedUpdate`.
2. **Enemies.** A `std::vector<Enemy>` updated in `onFixedUpdate`, exactly like particles,
   chasing the player with `glm::normalize(player - enemy)`.
3. **Tilemaps from a file.** Load a map made in the Tiled editor (`.tmj` is JSON) and draw
   the visible tiles from a tileset atlas, just like step 4.
4. **More screens.** A Controls screen in Options (the rebinding from step 13, as a
   menu), a high-score table kept in `save.json()` as a list, or a "Get ready... 3, 2, 1"
   state that doesn't pause the game but stops the player moving until it pops.
5. **A level editor.** Grow the debug UI into a tool: click the world (convert the mouse
   with `camera_.screenToWorld`) to place coins when a "Place coins" checkbox is on.
6. **Music and atmosphere.** A track with `playMusic`, a quieter one for the title, and a
   crossfade between them in the title state's `onEnter`/`onExit`.
7. **Physics.** Box2D v3 for proper collisions and rigid bodies, stepped in
   `onFixedUpdate`.
