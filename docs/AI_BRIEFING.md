# Game2D — AI briefing

> **For the AI assistant.** This file is attached at the start of every chat about a game
> built on the Game2D framework. It is the working summary of the framework: what
> exists, how to use it correctly, the conventions, and how this user likes to work.
> Read it fully before writing code. For more depth, ask for `docs/ARCHITECTURE.md`
> (design and internals), `docs/TUTORIAL.md` (the demo built step by step) or
> `docs/BUILDING.md` (Windows, Linux, Steam Deck builds).
>
> Framework state: **2026-11-03**. If the user's copy of the framework is older or newer,
> trust the source code over this file.

## 1. What it is

An engine-free C++20 starter for **2D games** on Windows, Linux and Steam Deck. The game
owns all the code: a thin platform layer (SDL3), a Vulkan 1.3 renderer, a batched sprite
renderer with cameras and TrueType text, audio with effects, and a debug UI. A playable
demo (`DemoGame`: a slime collecting coins, with particles, a HUD, sound zones and debug
panels) shows how it fits together.

- **Language and build:** C++20, CMake ≥ 3.22 with presets (`debug`, `relwithdebinfo`,
  `release`, `vs2022`, `vs2026`). Every dependency is downloaded by CMake at a pinned
  version (`cmake/Dependencies.cmake`).
- **Platforms:** Windows (Visual Studio 2022/2026), Linux, Steam Deck (SteamOS, Mesa RADV).
- **Libraries:** SDL3 3.4.16 (window, input, gamepads), Vulkan 1.3 through volk, VMA 3.3.0
  (GPU memory), GLM 1.0.1 (maths), stb_image and stb_truetype (images, fonts), miniaudio
  0.11.23 (audio), Dear ImGui 1.92.9 (debug UI).
- **Shared code:** `core/`, `gfx/`, `audio/`, `debugui/` and `render2d/` are identical in
  Game2D and Game3D (this is Game2D; its sibling is Game3D). Game2D's `render2d` is the main renderer.

## 2. Project layout

```
src/
  main.cpp              AppConfig + run the game (swap DemoGame for your game here)
  core/                 Application (loop, hooks), Window, Input, Paths, Log
  gfx/                  Vulkan: VulkanContext, Swapchain, Renderer, Buffer, Image,
                        Texture, Pipeline(Builder), Descriptors
  render2d/             SpriteBatch, Camera2D, Font
  audio/                AudioEngine (miniaudio): sounds, music, 3D, effects, Doppler
  debugui/              DebugUI (Dear ImGui)
  game/                 DemoGame (the example); your game goes here
shaders/                sprite.vert/.frag, imgui.frag (GLSL, compiled at build time)
assets/                 fonts/ (Roboto), sounds/, textures/ (copied next to the exe)
cmake/                  Dependencies.cmake, Shaders.cmake
docs/                   ARCHITECTURE, BUILDING, TUTORIAL, AI_BRIEFING (this file)
```

New `.cpp`/`.h` files anywhere under `src/` are picked up automatically
(`GLOB_RECURSE ... CONFIGURE_DEPENDS`); no CMake edits are needed. New shaders in
`shaders/` are compiled to `<exe>/shaders/<name>.spv` automatically. Everything in `assets/`
is copied next to the executable on every build.

## 3. A complete minimal game

This compiles and runs as is. It shows the shape every game follows: a `core::Game`
subclass with hooks, created in `main.cpp`.

**`src/game/MyGame.h`**

```cpp
#pragma once

#include "core/Application.h"
#include "render2d/Camera2D.h"
#include "render2d/SpriteBatch.h"

#include <memory>
#include <string>

class MyGame : public core::Game {
public:
    void onInit(core::Application& app) override;
    void onShutdown(core::Application& app) override;
    void onFixedUpdate(core::Application& app, float dt) override;
    void onUpdate(core::Application& app, float dt) override;
    void onRender(core::Application& app, gfx::FrameContext& frame) override;
    void onDebugUI(core::Application& app) override;

private:
    std::unique_ptr<render2d::SpriteBatch> batch_;
    render2d::Camera2D camera_;    // the world
    render2d::Camera2D hudCamera_; // the screen: 1 unit = 1 pixel
    render2d::Font font_;
    gfx::Texture hero_;
    std::string jumpSound_;

    glm::vec2 heroPos_{0.0f}, prevHeroPos_{0.0f}; // simulation state (fixed step)
    glm::vec2 moveInput_{0.0f};                   // read in onUpdate, used in onFixedUpdate
    float speed_ = 150.0f;                        // world units per second
};
```

**`src/game/MyGame.cpp`**

```cpp
#include "game/MyGame.h"

#include "core/Paths.h"

#include <glm/geometric.hpp>

using namespace render2d;

void MyGame::onInit(core::Application& app)
{
    batch_ = std::make_unique<SpriteBatch>(app.gpu(), app.renderer());
    gfx::TextureDesc pixelArt;
    pixelArt.filter = VK_FILTER_NEAREST; // crisp pixels
    pixelArt.mipmaps = false;
    hero_ = gfx::Texture::fromFile(app.gpu(), core::assetPath("textures/player.png"), pixelArt);
    font_ = Font::load(app.gpu(), core::assetPath("fonts/Roboto-Medium.ttf"));
    jumpSound_ = core::assetPath("sounds/whoosh.wav");
    app.audio().preload(jumpSound_);
    camera_.referenceHeight = 360.0f; // always show 360 world units vertically
}

void MyGame::onShutdown(core::Application&)
{
    batch_.reset(); // GPU is idle here: safe to destroy GPU objects
}

void MyGame::onFixedUpdate(core::Application&, float dt)
{
    prevHeroPos_ = heroPos_; // for interpolation in onRender
    heroPos_ += moveInput_ * speed_ * dt;
}

void MyGame::onUpdate(core::Application& app, float)
{
    core::Input& in = app.input();
    if (in.keyPressed(SDL_SCANCODE_ESCAPE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_BACK))
        app.quit();

    glm::vec2 move(0.0f);
    if (in.keyDown(SDL_SCANCODE_A)) move.x -= 1.0f;
    if (in.keyDown(SDL_SCANCODE_D)) move.x += 1.0f;
    if (in.keyDown(SDL_SCANCODE_W)) move.y -= 1.0f; // +Y is down
    if (in.keyDown(SDL_SCANCODE_S)) move.y += 1.0f;
    if (glm::length(move) > 1.0f)
        move = glm::normalize(move);
    if (glm::length(in.leftStick()) > glm::length(move))
        move = in.leftStick();
    moveInput_ = move;

    if (in.keyPressed(SDL_SCANCODE_SPACE) || in.gamepadPressed(SDL_GAMEPAD_BUTTON_SOUTH))
        app.audio().play(jumpSound_, {.volume = 0.8f});

    camera_.position = glm::mix(prevHeroPos_, heroPos_, app.interpolationAlpha());
}

void MyGame::onRender(core::Application& app, gfx::FrameContext& frame)
{
    const glm::vec2 viewport(float(frame.extent.width), float(frame.extent.height));

    // World
    camera_.setViewport(viewport.x, viewport.y);
    batch_->begin(frame, camera_.viewProjection());
    batch_->drawRect({-200.0f, -150.0f}, {400.0f, 300.0f}, Color(0.2f, 0.25f, 0.3f, 1.0f));
    Sprite hero;
    hero.position = glm::mix(prevHeroPos_, heroPos_, app.interpolationAlpha());
    hero.size = glm::vec2(32.0f);
    batch_->draw(hero_, hero);
    batch_->end();

    // HUD
    hudCamera_.setViewport(viewport.x, viewport.y);
    hudCamera_.position = viewport * 0.5f;
    batch_->begin(frame, hudCamera_.viewProjection());
    TextStyle text;
    text.size = 24.0f;
    text.snapToPixel = true;
    batch_->drawText(font_, "Hello!", {16.0f, 12.0f}, text);
    batch_->end();
}

void MyGame::onDebugUI(core::Application&)
{
    ImGui::Begin("Game");
    ImGui::DragFloat2("Hero", &heroPos_.x);
    ImGui::SliderFloat("Speed", &speed_, 0.0f, 500.0f);
    ImGui::End();
}
```

**`src/main.cpp`**

```cpp
#include "core/Application.h"
#include "game/MyGame.h"

#include <SDL3/SDL_main.h> // provides the platform entry point; include in one file only

int main(int, char**)
{
    core::AppConfig config;
    config.window.title = "My Game";
    config.renderer.depthBuffer = false;
    core::Application app(config);
    MyGame game;
    return app.run(game);
}
```

## 4. The game loop and hooks (`core/Application.h`)

`core::Application` owns the window, input, GPU, renderer, audio engine and debug UI,
and runs the loop. A game is a subclass of `core::Game`; every hook is optional:

| Hook | When | Use it for |
|---|---|---|
| `onInit(app)` | once, GPU ready | load textures, fonts, sounds; create renderers |
| `onFixedUpdate(app, dt)` | 0..n times per frame at a fixed rate (60 Hz) | gameplay simulation, physics, timers |
| `onUpdate(app, dt)` | once per frame, real frame time | input, camera, UI state, audio listener |
| `onDebugUI(app)` | after `onUpdate`, only while the debug UI is visible | ImGui windows (the only place to call ImGui) |
| `onPreRender(app, frame)` | before the main render pass | uploads, off-screen passes, compute |
| `onRender(app, frame)` | inside the main pass (cleared, viewport set) | all drawing |
| `onResize(app, w, h)` | swapchain size changed (pixels) | resize render targets |
| `onShutdown(app)` | after `vkDeviceWaitIdle` | destroy GPU objects (reset `unique_ptr`s) |

Frame order: events → `onFixedUpdate` × n → `onUpdate` → audio update → `onDebugUI` →
`onPreRender` → `onRender` → debug UI drawn on top → present.

**Fixed timestep with interpolation.** Simulate in `onFixedUpdate` and keep the previous
state. Render `glm::mix(prev, current, app.interpolationAlpha())` so motion is smooth at any
refresh rate. Read input in `onUpdate` into a member and apply it in `onFixedUpdate`.

**`core::Application`:** `window()`, `input()`, `gpu()` (`gfx::VulkanContext&`),
`renderer()` (`gfx::Renderer&`), `audio()` (`audio::AudioEngine&`), `debugUI()`
(`debugui::DebugUI*`, **null in Release builds**), `config()`, `time()` (seconds since
start), `fps()`, `interpolationAlpha()`, `quit()`.

**`core::AppConfig`** (set in `main.cpp`): `window` (`title`, `width`, `height`,
`resizable`, `fullscreen`), `renderer` (`vsync`, `depthBuffer`, `clearColor` as **linear**
RGBA, `framesInFlight`), `audio` (see §8), `debugUI` (see §9), `fixedTimestep` (1/60),
`validation`, `showFpsInTitle`, `f11TogglesFullscreen` (also Alt+Enter).

**Renderer:** `app.renderer().setVsync(bool)`, `vsync()`, `setClearColor(linear vec4)`,
`extent()`, `colorFormat()`, `depthFormat()`, `framesInFlight()`, `waitIdle()`.
`gfx::FrameContext` (passed to the render hooks): `cmd`, `frameIndex` (index per-frame
resources with it), `imageIndex`, `frameNumber`, `extent`.

## 5. Input (`core::Input`, `app.input()`)

- Keys by **scancode** (layout-independent): `keyDown`, `keyPressed`, `keyReleased`
  (`SDL_SCANCODE_W`, ...). Presses are latched, so short taps are never missed.
- Mouse (framebuffer **pixels**): `mouseDown/Pressed/Released(SDL_BUTTON_LEFT)`,
  `mousePosition()`, `mouseDelta()`, `mouseWheel()`, `setRelativeMouseMode(app.window().handle(), on)`.
- Gamepad (first connected; Steam Deck controls arrive as a gamepad through Steam Input):
  `hasGamepad()`, `gamepadName()`, `gamepadDown/Pressed/Released(SDL_GAMEPAD_BUTTON_SOUTH ...)`,
  `leftStick()`/`rightStick()` in [-1, 1] with **+Y down** and a radial dead zone,
  `leftTrigger()`/`rightTrigger()` in [0, 1], `rumble(low, high, ms)`, `stickDeadZone`.
- SOUTH = A (Xbox) / cross, EAST = B, WEST = X, NORTH = Y; BACK = View/Select.
- While the debug UI has the mouse over a window or a text field focused, those presses
  don't reach `Input` (releases always do).

## 6. Textures, files and logging

- `gfx::Texture::fromFile(app.gpu(), core::assetPath("textures/x.png"), desc)` (PNG, JPG,
  TGA, BMP; aborts with a message if missing), `fromPixels(gpu, w, h, rgba, desc)`,
  `solid(gpu, r, g, b, a)`. Move-only; `width()`, `height()`, `size()`, `valid()`.
- `gfx::TextureDesc`: `srgb` (true for colour images; false for data such as normal maps),
  `mipmaps`, `filter` (`VK_FILTER_NEAREST` for pixel art), `wrap`
  (`VK_SAMPLER_ADDRESS_MODE_REPEAT` / `CLAMP_TO_EDGE`), `debugName`.
- Paths: `core::assetPath("sounds/x.wav")` → `<exe>/assets/sounds/x.wav`,
  `core::shaderPath("x.frag.spv")`, `core::basePath()`. Never rely on the working directory.
- Logging (`core/Log.h`): `LOG_INFO/LOG_WARN/LOG_ERROR/LOG_DEBUG(printf-style)`;
  `core::fatal(fmt, ...)` logs, shows a message box and aborts.

## 7. Sprites and text (`render2d`)

**`render2d::SpriteBatch`** (`SpriteBatch(app.gpu(), app.renderer())`): batched textured
quads, one draw call per run of the same texture.

- `begin(frame, camera.viewProjection())` … `end()`. Several begin/end pairs per frame are
  fine (world, then HUD).
- `draw(texture, Sprite)`; `draw(texture, position, scale = 1, rotation = 0, color)` (native
  size, centred); `drawRect(topLeft, size, color)`; `drawRectOutline(topLeft, size,
  thickness, color)`; `drawLine(a, b, thickness, color)`;
  `drawText(font, utf8, topLeftOfFirstLine, TextStyle)` (returns the size drawn).
- `Sprite`: `position` (of the pivot), `size`, `origin` (pivot, normalised: 0,0 = top-left,
  0.5 = centre), `rotation` (radians, clockwise on screen), `color`, `uv`.
- `UvRect::fromPixels(texture, x, y, w, h)` for sprite-sheet frames; `.flippedX()`,
  `.flippedY()`.
- Draw order = call order (no depth sorting). `stats()` gives sprites and draw calls.
- `render2d::Color` is `glm::vec4`, **sRGB** 0..1 like an image editor; `colors::White`,
  `Black`, `Red`, `Green`, `Blue`, `Yellow`.

**`render2d::Camera2D`:** `position` (world point at the screen centre), `zoom` (>1 zooms
in), `rotation`, `referenceHeight` (if > 0, always shows that many world units vertically,
so 720p, Deck 800p and 4K look the same). Call `setViewport(w, h)` each frame from
`frame.extent`. `viewProjection()`, `screenToWorld(pixels)`, `worldToScreen(world)`,
`visibleBounds(min, max)` (for culling), `scale()` (pixels per world unit).
**HUD camera:** `referenceHeight = 0` and `position = viewport * 0.5f` gives 1 unit =
1 pixel with (0, 0) at the top-left.

**`render2d::Font`:** `Font::load(app.gpu(), core::assetPath("fonts/Roboto-Medium.ttf"),
FontDesc)` bakes characters into an atlas with stb_truetype (default 48 px, printable
ASCII + Latin-1; UTF-8 strings; characters not baked show as `?`). `measure(text, size)`,
`lineHeight(size)`, `ascent(size)`, `texture()`. Text is sharpest at or below the baked size.
`TextStyle`: `size` (world units; pixels with the HUD camera), `color`, `align`
(`TextAlign::Left/Center/Right` relative to the position), `lineSpacing`, `snapToPixel`
(use with the HUD camera). `'\n'` starts a new line. The project ships Roboto Medium
(Apache 2.0).

## 8. Audio (`audio::AudioEngine`, `app.audio()`)

miniaudio underneath. Never fatal: with no device (or `GAME_AUDIO=0`) every call is a
harmless no-op. WAV, FLAC, MP3, OGG Vorbis. Call from the game thread only.

**Sound effects:** decoded on first play and cached; `preload(path)` in `onInit` avoids a
hitch. `SoundHandle h = play(path, PlayParams)`; handles are never reused, and a stale
handle is simply ignored. `stop(h, fadeSeconds)`, `stopAllSounds()`, `isPlaying(h)`,
`setVolume/setPitch/setPan(h, x)`, `setPosition(h, vec3)`, `unload(path)`. Up to 64 at once
(`AudioDesc::maxVoices`); when full, the oldest one-shot is cut (loops never are).

`PlayParams`: `volume`, `pitch` (2 = an octave up and twice as fast), `pan` (-1..1,
non-spatial only), `loop`, `spatial`, `position`, `minDistance`, `maxDistance`, `rolloff`,
`lowPassHz` (this sound muffled; `audio::kLowPassOff` = off), `reverbSend` (0 = stays
dry, e.g. UI sounds), `doppler` (this sound's Doppler amount; 0 = none).

**3D sound and Doppler:** `setListener(position, forward, up)` every frame (usually the
camera, right-handed, +Y up). Spatial sounds attenuate by distance (inverse model) and pan
by direction; use **mono** files. Doppler is automatic: velocities are estimated from
`setPosition`/`setListener` calls each frame (teleports ignored, speeds capped).
`setVelocity(h, v)` / `setListenerVelocity(v)` override the estimate. `setDopplerFactor`
(0 off, 1 real, 2-3 exaggerated), `setSpeedOfSound` (world units/s; 343 = metres),
`dopplerPitch(h)`. 2D sounds are usually non-spatial with `pan`; for distance and Doppler in 2D, play them as spatial in the X/Y plane with the listener at the camera.

**Music:** one streamed track at a time. `playMusic(path, MusicParams{volume, loop = true,
fadeInSeconds, fadeOutPreviousSeconds = 1})` (crossfades from the current track),
`stopMusic(fade)`, `pauseMusic()`, `resumeMusic()`, `isMusicPlaying()`. Put tracks in
`assets/music/`. Music is never spatial and never gets reverb.

**Mixer:** `setMasterVolume`, `setSfxVolume`, `setMusicVolume` (default 0.8), `setMuted`
(keeps the volumes), plus the getters.

**Effects** (every change glides; default 0.3 s, pass `glideSeconds` to change):
- Per bus (`audio::Bus::Sfx`, `Bus::Music`, `Bus::Master`): `setLowPass(bus, hz)` (muffle;
  ~700 Hz = under water), `setHighPass(bus, hz)` (thin/radio; `kHighPassOff` = off),
  `setEcho(bus, EchoParams{delaySeconds, feedback, mix})` (mix 0 = off).
- Reverb (shared; sound effects send to it, music never): `setReverb(ReverbParams)`;
  `ReverbParams::preset(ReverbPreset::Off/Room/Hall/Cave)` returns plain values (`level`,
  `roomSize`, `damping`, `width`) that can be tweaked. Its tail rings on after the sound ends.
- Per sound: `setLowPass(handle, hz)` (occlusion: the game decides when something blocks a
  sound), `setReverbSend(handle, amount)`.
- `resetEffects()`, getters `lowPass(bus)`, `highPass(bus)`, `echo(bus)`, `reverb()`.
- Change effects on **transitions** (entering a zone), not every frame.

`AudioDesc` (in `AppConfig::audio`): `enabled`, `masterVolume`, `sfxVolume`, `musicVolume`,
`maxVoices`, `speedOfSound`, `dopplerFactor`, `noDevice` (tests: mix in memory with
`readFrames`).

## 9. Debug UI (`debugui::DebugUI`, Dear ImGui)

- **Debug builds only** by default (`AppConfig::debugUI.enabled`); not created in
  RelWithDebInfo/Release, where `app.debugUI()` is null. Toggle with **`** (the key below
  Esc; `DebugUIDesc::toggleKey`). On the Deck: bind a button to that key in Steam Input; the
  touchscreen acts as the mouse.
- Write ImGui code **only** in `Game::onDebugUI(app)`. `imgui.h` comes with
  `core/Application.h`. Always pair `ImGui::Begin` with `ImGui::End`, even when `Begin`
  returns false.
- The built-in **Engine** window: frame-time graph, GPU and swapchain info, vsync and
  fullscreen, audio volumes, all audio effects with live sliders, input devices, and a switch
  for ImGui's demo window (the best widget reference).
- `app.debugUI()->texture(gfx::Texture)` returns an `ImTextureID` for `ImGui::Image`.
- Useful patterns: `SetNextWindowPos(..., ImGuiCond_FirstUseEver)`,
  `ImGuiWindowFlags_AlwaysAutoResize`, `SliderFloat(..., ImGuiSliderFlags_Logarithmic)`,
  `ColorEdit3("Fog", &v.x)`, `SliderAngle` (radians shown as degrees), `"Label##id"` for
  repeated labels, `ImGui::GetStyle().FontScaleDpi` for DPI-aware sizes.
- The layout isn't saved between runs (no `imgui.ini`).

## 10. Conventions and rules for writing code here

- **Read input in `onUpdate`, simulate in `onFixedUpdate`, draw in `onRender`.**
  Interpolate rendered positions with `interpolationAlpha()`.
- **GPU object lifetime:** create in `onInit`; destroy in `onShutdown` (the GPU is idle)
  or in the game's destructor. Never destroy a texture/buffer mid-game that an in-flight
  frame may still use; call `app.renderer().waitIdle()` first. GPU wrappers (`Texture`,
  `Buffer`, `Image`, `Pipeline`, `Mesh`, `Font`) are RAII and move-only.
- **Colours:** author tints in **sRGB** (as in an image editor); the shaders convert to
  linear. Exception: `RendererConfig::clearColor` / `setClearColor` take **linear** values.
- **Sizes for different screens:** HUD sizes scale with `max(1, viewportHeight / 720)` in
  the demos; ImGui sizes with `ImGui::GetStyle().FontScaleDpi`.
- **Steam Deck:** 1280×800, gamepad-only must work, text must be readable on 7".
- **Errors:** unrecoverable → `core::fatal`; missing optional things → `LOG_WARN` and carry
  on. Vulkan calls go through `VK_CHECK` in framework code.
- **Validation layers** are on in Debug builds (`GAME_VALIDATION=0/1` overrides). Code must
  run with **zero** validation messages. `GAME_MAX_FRAMES=N` quits after N frames (tests).
- **Style:** C++20, 4-space indent, `camelCase` functions, `trailingUnderscore_` members,
  `kConstants`, namespaces per module (`core`, `gfx`, `render2d`, `audio`,
  `debugui`). Comments explain *why*. Warnings are treated seriously (MSVC `/W4`;
  GCC/Clang `-Wall -Wextra`).
- **Framework vs game code:** game code lives in `src/game/` (and new folders the game
  needs). If a game needs a change to the shared framework code (`core/`, `gfx/`, `audio/`,
  `debugui/`, `render2d/`), make it carefully, keep it general, and
  **tell the user**, so the change can be brought back to the base frameworks (both 2D and
  3D share that code).

## 11. How this user works (important)

- The user develops on **Windows in Visual Studio 2022** ("Open Folder" with the CMake
  presets) and also targets Linux and Steam Deck. They are learning as they go: explain
  clearly, keep a friendly tone, and say exactly which files changed.
- **Base frameworks** live at `F:\Pojects\Claude\Game2D` and `F:\Pojects\Claude\Game3D`
  (the folder is really spelled "Pojects"). Work is written to those original folders only.
  The user copies to `WorkingVersion`, `TestingVersion`, `GITHUB\2DBaseFramework` /
  `GITHUB\3DBaseFramework`, `BackUps` and an external drive themselves. Each game is its own
  copy of a framework; ask where it lives if that isn't clear.
- **Before overwriting any file on the user's computer, check it hasn't changed since you
  last wrote it** (compare checksums). The user sometimes edits files (they have edited
  `TUTORIAL.md`); never overwrite their changes.
- At the end of a change, **list the files to copy** and remind them to leave out the
  `build` and `.vs` folders, which are tied to their location.
- **Visual Studio pitfall:** don't add files with *Add → New Item* in the *CMake Targets
  View*: it writes the file into CMake scripts and breaks the configure. Add files with File
  Explorer or the *Folder View*. If a copied project fails with "CMakeCache.txt directory …
  is different", delete its `build` (and `.vs`) folder.
- **Testing before handing over:** compile with warnings as errors, run with validation
  layers (SwiftShader works in a cloud container), check behaviour (including audio, by
  mixing in memory), and look at screenshots. Say plainly what couldn't be tested (e.g. not
  yet built in Visual Studio).
- Docs are Markdown in `docs/`, with Mermaid diagrams (validate with `mmdc`). When a
  feature is added, update `README.md`, `docs/ARCHITECTURE.md`, `docs/BUILDING.md` if
  relevant, the tutorial, **and this briefing**.

## 12. Known limitations (as of 2026-11-03)

- Text is baked at one size per `Font` (for big or zooming text, bake larger or move to MSDF).
- No bindless textures: batches break on texture changes (use atlases).
- No tilemap loader, animation system or particles system beyond what the demo shows:
  sprite-sheet animation is `UvRect::fromPixels` + a timer; particles are a vector.
- One audio listener (no split-screen); one music track at a time; one shared reverb.
- The debug UI draws inside the game window (no ImGui docking or separate OS windows) and
  doesn't save its layout.
- No deferred GPU resource destruction (destroy only when idle); uploads are blocking
  (`immediateSubmit`); single graphics queue.
- No physics, no scene/entity system, no asset hot-reload, no save-game system: the game
  writes these (or adds a library; Box2D v3 for 2D, Jolt for 3D are good fits).

## 13. Where to look

| Need | File |
|---|---|
| Design, internals, diagrams | `docs/ARCHITECTURE.md` |
| Building on Windows, Linux, Steam Deck; shipping on Steam; troubleshooting | `docs/BUILDING.md` |
| The demo, rebuilt step by step | `docs/TUTORIAL.md` |
| Overview, controls, quick start | `README.md` |
| A complete example game | `src/game/DemoGame.h/.cpp` |
| Every ImGui widget with code | debug UI → Engine → "Dear ImGui demo window" |
