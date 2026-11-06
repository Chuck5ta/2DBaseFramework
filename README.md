# Game2D — C++20 / Vulkan 1.3 starter for 2D games

A small, engine-free base for 2D games on **Windows, Linux and Steam Deck**.
You own all the code: a thin platform layer (SDL3), a Vulkan 1.3 renderer,
a batched sprite renderer and audio, plus a playable demo showing how they fit together.

| Layer | What it does | Library |
|---|---|---|
| Platform | Window, keyboard/mouse, gamepads (incl. Steam Deck controls), hi-DPI | SDL3 |
| Input actions | Named actions ("jump") with rebindable keys/buttons/sticks, button prompts for the device in use (Xbox, PlayStation, Nintendo labels), buffered presses, bindings as JSON | nlohmann/json |
| Game states | A stack of screens (title, pause, game over) above the game: each can pause or hide what's below; safe push/pop/replace | your code |
| GPU | Instance/device setup, swapchain, frames in flight, buffers, textures, pipelines | Vulkan 1.3 via volk + VMA |
| 2D | Sprite batching, orthographic camera, atlas UVs, rect/line helpers | your code |
| Animation | Sprite sheets (grid or Aseprite JSON export), clips with per-frame timing, loop/once/ping-pong, frame events | nlohmann/json |
| Text | TrueType fonts baked into an atlas, UTF-8, kerning, alignment, world or screen space | stb_truetype |
| Audio | Sound effects (pitch, pan, volume), streamed music with crossfades, mixer, mute; effects: low-pass, high-pass, echo, reverb | miniaudio |
| Debug UI | In-game tool windows (Debug builds): frame graph, GPU info, volumes, your own sliders and buttons | Dear ImGui |
| Math / images | Vectors & matrices, PNG/JPG loading | GLM, stb_image |

All dependencies are downloaded automatically by CMake at a pinned version.

For a deeper look at how it all fits together, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
Step-by-step build instructions for Windows, Linux and Steam Deck are in [docs/BUILDING.md](docs/BUILDING.md).
To learn the framework by rebuilding the demo step by step, follow [docs/TUTORIAL.md](docs/TUTORIAL.md).
Starting a chat with an AI assistant about a game built on this framework? Attach
[docs/AI_BRIEFING.md](docs/AI_BRIEFING.md): a compact summary of the API, conventions and workflow.

---

## 1. Prerequisites

### Windows
- **Visual Studio 2022 or 2026** with the *Desktop development with C++* workload (includes CMake + Ninja)
- **Vulkan SDK** (recommended): <https://vulkan.lunarg.com/sdk/home>. Gives you the
  validation layers and `glslc`. Without it the project still builds (glslang is built
  from source, which needs **Python 3** on the PATH) but you lose validation messages.
- An up-to-date GPU driver.

### Linux (desktop)
Compiler, CMake ≥ 3.22, Ninja, Git, plus the headers SDL3 needs for X11 and Wayland.

Ubuntu / Debian:
```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev \
  libdecor-0-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev \
  libasound2-dev libpulse-dev \
  glslang-tools vulkan-validationlayers mesa-vulkan-drivers
```
Fedora: `sudo dnf install gcc-c++ cmake ninja-build git libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel libXScrnSaver-devel libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel mesa-libEGL-devel libdecor-devel systemd-devel dbus-devel alsa-lib-devel pulseaudio-libs-devel glslang vulkan-validation-layers`

Arch: `sudo pacman -S base-devel cmake ninja git libx11 libxext libxrandr libxcursor libxi libxss libxtst libxkbcommon wayland wayland-protocols libdecor glslang vulkan-validation-layers`

### Steam Deck
See [section 6](#6-steam-deck).

---

## 2. Build & run

### Command line (any platform)
```bash
cmake --preset debug          # configure (first run downloads dependencies)
cmake --build --preset debug  # build
./build/debug/bin/Debug/Game2D          # Linux
build\debug\bin\Debug\Game2D.exe        # Windows
```
Presets: `debug`, `relwithdebinfo` (profiling), `release` (no console window on Windows).

On Windows, run the command-line build from a **Developer PowerShell for VS** so the
compiler is on the PATH.

### Visual Studio
*File → Open → Folder…* and pick this folder. VS reads `CMakePresets.json`; choose the
`debug` preset, select **Game2D.exe** as the startup item, press F5.
Or generate a classic solution: `cmake --preset vs2022` (or `vs2026`) and open the `.sln` in `build/`.

### VS Code / CLion
Both understand `CMakePresets.json` directly (VS Code: install *CMake Tools*).

The output folder contains everything the game needs:
```
bin/<Config>/
  Game2D(.exe)
  shaders/*.spv      compiled from shaders/ on every build
  assets/...         copied from assets/ on every build
```
Files are found relative to the executable, so it runs from any working directory.

For a distributable folder: `cmake --install build/release --prefix dist`.

---

## 3. Project layout

```
src/
  main.cpp              configure window/renderer, run the game
  core/                 platform layer (shared with Game3D)
    Application         owns everything, runs the main loop
    Window              SDL window, Vulkan surface, fullscreen
    Input               keyboard, mouse, gamepad (pressed / down / released)
    Actions             named input actions: rebindable, with button prompts
    GameState           title / pause / game-over screens on a stack (app.states())
    Paths               shaderPath(), assetPath()
    Log.h               LOG_INFO/WARN/ERROR, core::fatal()
  audio/                sound effects, music, 3D sound (shared with Game3D)
    AudioEngine         miniaudio: cached effects, streamed music, listener, mixer
  debugui/              debug overlay, ` key (shared with Game3D)
    DebugUI             Dear ImGui setup; your windows go in Game::onDebugUI
  gfx/                  Vulkan layer (shared with Game3D)
    VulkanContext       instance, GPU selection, device, queues, VMA, immediateSubmit
    Swapchain           creation/recreation, per-image semaphores
    Renderer            frames in flight, acquire/submit/present, main pass
    Buffer, Image       RAII GPU memory
    Texture             image + sampler + ready-to-bind descriptor set
    Pipeline            PipelineBuilder (dynamic rendering, no render passes)
    Descriptors         layout builder, growable allocator, writer
  render2d/
    SpriteBatch         batched quads, 1 draw call per texture run
    Camera2D            ortho camera, zoom/rotation, screen<->world
    Font                .ttf/.otf baked into a texture atlas; drawn with SpriteBatch::drawText
    Animation           SpriteSheet (grid / Aseprite JSON), clips, Animator
  game/
    DemoGame            the example — replace with your game
shaders/                GLSL; every *.vert/*.frag/*.comp is compiled automatically
assets/                 copied next to the exe (fonts/: Roboto Medium, Apache 2.0;
                        sounds/: demo effects, CC0; sprites/: animated sheets, CC0)
cmake/                  dependency + shader build logic
```

`core/`, `gfx/`, `audio/`, `debugui/` and `render2d/` are identical in Game2D and Game3D, so improvements can be
copied between them (or later moved into a shared library).

---

## 4. How a frame works

```
Application::run()
 ├─ poll SDL events → Input, window resize → Renderer
 ├─ Actions update             named actions from this frame's input
 ├─ States: apply push/pop    queued last frame (onEnter/onExit, onPause/onResume)
 │    (below, "Game::" = the Game, then each state, skipping paused or hidden ones)
 ├─ Game::onFixedUpdate(dt)   0..n times at a fixed 60 Hz (gameplay/physics)
 ├─ Game::onUpdate(dt)        once per frame (input, camera)
 └─ Renderer
     ├─ beginFrame()          wait for frame slot, acquire swapchain image
     ├─ Game::onPreRender()   work outside the main pass (uploads, offscreen)
     ├─ beginMainPass()       dynamic rendering, clear, viewport
     ├─ Game::onRender()      your draw calls
     └─ endMainPass(), endFrame()   submit + present
```

* **Frames in flight:** 2 by default. Anything you write from the CPU each frame
  (vertex data, uniforms) needs one copy per frame — index it with `frame.frameIndex`
  (see `SpriteBatch`).
* **Fixed timestep + interpolation:** simulate in `onFixedUpdate`, keep the previous
  state, and render `mix(prev, current, app.interpolationAlpha())` for smooth motion on
  any refresh rate (the demo does this for the player).
* **Colour:** the swapchain is sRGB, shaders output linear colour. Colours you pass to
  `SpriteBatch` are sRGB (as in an image editor) and converted for you.
* **Resizing, minimising, vsync toggling, fullscreen (F11 / Alt+Enter)** are handled.

---

## 5. Making your game

1. Copy `game/DemoGame.*` as a starting point or write a new `core::Game` subclass.
2. In `main.cpp`, construct your game instead of `DemoGame`.
3. Create GPU resources in `onInit`, draw in `onRender`:

```cpp
void MyGame::onInit(core::Application& app) {
    batch_ = std::make_unique<render2d::SpriteBatch>(app.gpu(), app.renderer());
    hero_  = gfx::Texture::fromFile(app.gpu(), core::assetPath("textures/hero.png"),
                                    {.mipmaps = false, .filter = VK_FILTER_NEAREST}); // pixel art
    camera_.referenceHeight = 360;   // same view on 720p, Deck 800p and 4K
}

void MyGame::onRender(core::Application&, gfx::FrameContext& frame) {
    camera_.setViewport(float(frame.extent.width), float(frame.extent.height));
    batch_->begin(frame, camera_.viewProjection());
    batch_->draw(hero_, heroPosition_);                      // centred, native size
    render2d::Sprite s{.position = {100, 50}, .size = {16, 16},
                       .uv = render2d::UvRect::fromPixels(sheet_, 32, 0, 16, 16)};
    batch_->draw(sheet_, s);                                 // sprite-sheet frame
    batch_->end();
}
```

Tips
- Draw order = submission order. Group sprites by texture (or use an atlas) to keep
  draw calls low; `batch.stats()` shows sprites/draw calls.
- Use `begin()/end()` twice per frame for world + HUD with different cameras.
- Animation: `sheet_ = render2d::SpriteSheet::loadAseprite(app.gpu(), core::assetPath("sprites/hero.json"))`,
  then per object `anim_.play(sheet_, "walk"); anim_.update(dt); sprite.uv = anim_.uv(facingLeft);`.
  Clips come from Aseprite tags; `eventFired("hit")` for events on frames (tutorial step 12).
- New shaders: drop `foo.vert`/`foo.frag` in `shaders/`, load
  `core::shaderPath("foo.vert.spv")`.
- Destroying a texture/buffer that an in-flight frame still uses is an error; call
  `app.renderer().waitIdle()` first (or destroy in `onShutdown`).
- Sound: `app.audio().play(core::assetPath("sounds/jump.wav"), {.volume = 0.7f})`;
  music: `app.audio().playMusic(core::assetPath("music/theme.ogg"))`. WAV, FLAC, MP3, OGG.
- Effects: `app.audio().setReverb(audio::ReverbParams::preset(audio::ReverbPreset::Cave))`,
  `app.audio().setLowPass(audio::Bus::Sfx, 700.0f)` (muffled), `setEcho`, `setHighPass`.
  Changes glide smoothly. Try them all live in the debug UI (Engine → Audio → Audio effects).
- Game states: derive from `core::GameState`, then `app.states().push<PauseState>(...)`.
  By default a state pauses the game below it (its `onUpdate`/`onFixedUpdate` stop) and
  lets it show through; `pop()` resumes. Changes apply at the start of the next frame, so
  a state can pop itself. See tutorial step 14.
- Input actions: define once in `onInit` with
  `app.actions().define("jump", {core::Binding::key(SDL_SCANCODE_SPACE), core::Binding::button(SDL_GAMEPAD_BUTTON_SOUTH)})`,
  then ask `actions.pressed("jump")`, `actions.vector("left", "right", "up", "down")`, or
  `actions.consume("jump")` in `onFixedUpdate` (a buffered press). `actions.prompt("jump")`
  gives "Space" or "A" for the help text; players can rebind with `capture()` + `addBinding()` (tutorial step 13).
- Debug UI: press **`** (below Esc) in a Debug build. Add your own windows by overriding
  `onDebugUI(core::Application&)` and calling ImGui there, e.g.
  `ImGui::SliderFloat("Speed", &speed_, 0.0f, 500.0f);`.
- Validation layers are on in Debug builds (set `GAME_VALIDATION=0/1` to override). Keep
  the console clean of validation errors.

### Demo controls
| Action | Keyboard / mouse | Gamepad / Steam Deck |
|---|---|---|
| Play / start a 60-second round | Title screen: **Play** | |
| Move | WASD / arrows | Left stick / D-pad |
| Particle burst (and the slime's burst animation) | Space | A |
| Zoom | Mouse wheel, + / − | Bumpers |
| Mute sound | M | |
| Debug UI (Debug builds) | ` (key below Esc) | |
| Rebind controls | Debug UI → **Controls** window | |
| Show/hide help | H | |
| Toggle vsync | V | |
| Fullscreen | F11 / Alt+Enter | |
| Menus: choose / select / back | Up/Down (or mouse), Enter or Space (or click), Backspace | D-pad or stick, A, B |
| Pause (Quit is on the title screen) | Esc | Start (Menu) |

---

## 6. Steam Deck

The Deck runs SteamOS (Arch-based Linux) with an AMD GPU using Mesa's RADV
Vulkan driver, which fully supports everything used here. Screen: 1280×800.
Steam Input presents the built-in controls as a standard gamepad, so `Input`'s
gamepad API just works.

**Getting a build onto the Deck — pick one:**
1. **Build on the Deck (quick testing).** In Desktop Mode, SteamOS's system partition is
   read-only, so build inside a container: `distrobox create -i archlinux` (or
   `ubuntu:24.04`), enter it, install the Linux packages above, then build normally.
   Add the resulting executable to Steam with *Add a Non-Steam Game* to run it in Game Mode.
2. **Build on a Linux PC and copy the `bin/Release` folder** to the Deck (e.g. via
   `scp`, a USB stick or Steam's dev kit tools). Building on an older/LTS distro
   gives the most compatible binary.
3. **For release on Steam,** build inside Valve's **Steam Linux Runtime 4.0 SDK** container
   (<https://gitlab.steamos.cloud/steamrt/steamrt4/sdk>) so the binary runs on every SteamOS
   and Linux Steam installation.

Full step-by-step instructions: [docs/BUILDING.md](docs/BUILDING.md#4-steam-deck).

Also handy: the **Steam Deck devkit client** (Steamworks docs → *Steam Deck → Developing
for Steam Deck*) deploys and runs builds from your PC over the network.

The Windows build also runs on the Deck through Proton, but shipping the native Linux build is better.

Things worth testing on the Deck: 800p UI scale (`referenceHeight` helps), 40/60 Hz frame
limits, suspend/resume (swapchain recreation), on-screen keyboard if you need text input.

---

## 7. Where to go next

- **Music:** add a track to `assets/music/` and call `app.audio().playMusic(...)`.
- **Tilemaps:** load Tiled (`.tmj`) maps, draw visible tiles from an atlas.
- **Physics:** Box2D v3.
- **Tools in the debug UI:** grow `onDebugUI` into a level editor or a tuning panel that saves to a file.
- **Bindless textures** (descriptor indexing) to batch across textures in one draw.
