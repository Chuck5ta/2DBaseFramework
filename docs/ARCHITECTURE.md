# Game2D — Architecture Overview

> Status: describes the framework as of version 0.1.0.
> Audience: anyone extending the framework or building a game on top of it.
> Diagrams use [Mermaid](https://mermaid.js.org/); they render on GitHub/GitLab and in
> VS Code with the *Markdown Preview Mermaid Support* extension.

---

## 1. Purpose and design goals

Game2D is a minimal, engine-free foundation for 2D games written in C++20, targeting
**Windows, Linux and Steam Deck** with **Vulkan 1.3** for graphics.

| Goal | How it is achieved |
|---|---|
| You own and understand all the code | Small codebase (~4k lines including comments), no engine or framework; only thin, well-known libraries |
| Cross-platform from day one | SDL3 hides OS differences; no platform `#ifdef`s in game code |
| Modern, simple Vulkan | Vulkan 1.3 *dynamic rendering* (no render passes / framebuffers) and *synchronization2* |
| Hard to misuse | RAII, move-only wrappers for every GPU object; fatal errors are loud and descriptive |
| Fast iteration | Shaders and assets rebuild/copy automatically; new source files are picked up by CMake |
| Shippable | Static SDL3, Vulkan loaded at runtime, executable finds its files relative to itself |

Non-goals: being a general-purpose engine, an editor, a scene graph, or an ECS. Those are
decisions for the game built on top.

---

## 2. Layered structure

The code is split into four layers. Each layer only depends on the layers below it.

```mermaid
flowchart TD
    game["game/  (DemoGame or your game)"]
    r2d["render2d/  SpriteBatch · Camera2D"]
    core["core/  Application · Window · Input · Paths · Log"]
    gfx["gfx/  VulkanContext · Swapchain · Renderer · Buffer · Image · Texture · Pipeline · Descriptors"]
    third["Third party: SDL3 · volk · Vulkan-Headers · VMA · GLM · stb_image"]

    game --> r2d
    game --> core
    r2d --> gfx
    core --> gfx
    gfx --> third
    core --> third
```

| Layer | Namespace | Responsibility | Shared with Game3D |
|---|---|---|---|
| `src/core` | `core` | Program lifetime, main loop, window, input, file paths, logging | Yes, identical |
| `src/gfx` | `gfx` | Everything Vulkan: device, swapchain, frames, memory, textures, pipelines | Yes, identical |
| `src/render2d` | `render2d` | 2D-specific rendering on top of `gfx` | No |
| `src/game` | (global) | The actual game; replaceable | No |

> `core` and `gfx` reference each other in one place only: `Application` owns the
> `VulkanContext` and `Renderer`, and `Window` creates the Vulkan surface. `gfx` itself
> depends only on `core::Window` (for the surface/size) and `core/Log.h`.

### Source map

```
src/
  main.cpp                    builds AppConfig, creates Application + Game, runs
  core/
    Application.h/.cpp        Game interface, AppConfig, main loop, fixed timestep
    Window.h/.cpp             SDL window, pixel size, fullscreen, Vulkan surface
    Input.h/.cpp              keyboard / mouse / gamepad state per frame
    Paths.h/.cpp              basePath(), shaderPath(), assetPath()
    Log.h                     LOG_* macros, core::fatal()
  gfx/
    VkCommon.h                single Vulkan include (vulkan.h + volk + VMA), VK_CHECK
    Implementations.cpp       VMA and stb_image implementation units
    VulkanContext.h/.cpp      instance, debug messenger, GPU selection, device, VMA
    Swapchain.h/.cpp          swapchain + image views + per-image semaphores
    Renderer.h/.cpp           frames in flight, acquire/submit/present, main pass
    Buffer.h/.cpp             VkBuffer + VMA allocation
    Image.h/.cpp              VkImage + view, imageBarrier() helper
    Texture.h/.cpp            Image + sampler + descriptor set, mip generation
    Pipeline.h/.cpp           Pipeline (RAII) + PipelineBuilder, SPIR-V loading
    Descriptors.h/.cpp        layout builder, growable allocator, writer
  render2d/
    Camera2D.h/.cpp           orthographic camera, screen <-> world
    SpriteBatch.h/.cpp        batched textured quads
  game/
    DemoGame.h/.cpp           example game
shaders/                      GLSL sources (compiled to SPIR-V at build time)
assets/                       runtime data (copied next to the executable)
cmake/                        Dependencies.cmake, Shaders.cmake
```

---

## 3. Module reference

### 3.1 core

**`core::Application`** owns every long-lived system and runs the main loop.
Construction order (and reverse destruction order) is significant:

1. `SDL_Init(VIDEO | GAMEPAD)`
2. `Window` → 3. `Input` → 4. `gfx::VulkanContext` → 5. `gfx::Renderer`

Its destructor tears down in exactly the reverse order, then calls `SDL_Quit()`.

**`core::Game`** is the interface a game implements. Every hook is optional:

| Hook | When | Typical use |
|---|---|---|
| `onInit` | once, after the GPU is ready | load textures, create `SpriteBatch`, set up state |
| `onFixedUpdate(dt)` | 0..n times per frame at a fixed rate (default 60 Hz) | gameplay simulation, physics |
| `onUpdate(dt)` | once per frame, real frame time | input handling, camera, animation |
| `onPreRender(frame)` | before the main render pass begins | uploads, off-screen passes, compute |
| `onRender(frame)` | inside the main pass (cleared, viewport set) | draw calls |
| `onResize(w, h)` | swapchain size changed | resize render targets / UI |
| `onShutdown` | after `vkDeviceWaitIdle`, before teardown | destroy GPU resources |

**`core::AppConfig`** groups configuration: window description, `gfx::RendererConfig`,
fixed timestep, validation on/off, FPS-in-title and fullscreen hotkeys.

**`core::Window`** wraps `SDL_Window` (created with `SDL_WINDOW_VULKAN |
SDL_WINDOW_HIGH_PIXEL_DENSITY`). It reports the *pixel* size (high-DPI aware), supplies
the Vulkan instance extensions SDL needs, and creates the `VkSurfaceKHR`.
Fullscreen is borderless "desktop" fullscreen (what gamescope on the Deck expects).

**`core::Input`** turns SDL events into per-frame state:

- `down` / `pressed` / `released` for keys (by scancode, layout-independent), mouse
  buttons and gamepad buttons. Presses are latched from events, so a tap shorter than a
  frame is never lost.
- Mouse position/delta in framebuffer pixels, wheel, relative mouse mode.
- First connected gamepad, with hot-plug fallback; radial dead zone on sticks; triggers
  0..1; rumble. Steam Deck controls arrive as a standard gamepad through Steam Input.
- Keys and buttons are released on focus loss to avoid "stuck" input.

**`core::Paths`** resolves everything relative to the executable directory
(`SDL_GetBasePath()`): `shaderPath("x.vert.spv")` → `<exe>/shaders/…`,
`assetPath("textures/x.png")` → `<exe>/assets/…`. The working directory never matters.

**`core/Log.h`** maps `LOG_INFO/WARN/ERROR/DEBUG` onto SDL's logger and provides
`core::fatal(fmt, …)`, which logs, shows a message box and aborts (see §9).

### 3.2 gfx

**`gfx::VulkanContext`**: one per application.

- Loads Vulkan at runtime through **volk** (`volkInitialize`); nothing links against
  `vulkan-1.dll` / `libvulkan.so`.
- Creates the instance (API 1.3), optionally with `VK_LAYER_KHRONOS_validation` and a
  `VK_EXT_debug_utils` messenger.
- Picks a GPU: must support Vulkan 1.3, `VK_KHR_swapchain`, `dynamicRendering`,
  `synchronization2`, and graphics + present queues. Scores discrete > integrated and
  prefers a single queue family for both.
- Creates the device (enables anisotropy and `fillModeNonSolid` when available) and the
  **VMA** allocator.
- Provides `immediateSubmit(lambda)` for blocking, load-time GPU work (uploads).
- Owns the shared **texture descriptor set layout** (binding 0 = combined image sampler,
  fragment stage) and a growable, free-able `DescriptorAllocator` for texture sets.

**`gfx::Swapchain`**: creation and recreation of the swapchain, its image views and
one *render-finished* semaphore **per swapchain image**.

- Format: `B8G8R8A8_SRGB` / `R8G8B8A8_SRGB` with sRGB non-linear colour space.
- Present mode: FIFO when vsync is on; otherwise MAILBOX → IMMEDIATE → FIFO.
- Image count: at least 3 (triple buffering), clamped to the surface limits.

**`gfx::Renderer`**: the frame loop on the GPU side (details in §5).
Configured by `RendererConfig { vsync, depthBuffer, clearColor, framesInFlight }`.
Game2D uses `depthBuffer = false`: draw order determines layering.

**`gfx::Buffer`**: `VkBuffer` + `VmaAllocation`. `MemoryUsage::GpuOnly`,
`CpuToGpu` (persistently mapped, sequential write) or `GpuToCpu` (mapped, cached).
`Buffer::createWithData()` uploads via a staging buffer. `write()` / `flush()` for
mapped buffers.

**`gfx::Image`**: `VkImage` + view + allocation for textures and render targets.
`imageBarrier()` records a single sync2 image barrier/layout transition.

**`gfx::Texture`**: a sampled RGBA8 image with its own sampler and **its own
descriptor set**, so a texture can be bound with a single call and needs no lookup tables.
Created from pixels (`fromPixels`), from a file via stb_image (`fromFile`) or as a solid
colour (`solid`). `TextureDesc` selects sRGB vs UNORM, mipmaps (generated on the GPU by
blitting), filtering (`NEAREST` for pixel art) and wrap mode.

**`gfx::Pipeline` / `gfx::PipelineBuilder`**: fluent builder for graphics pipelines
using dynamic rendering (`VkPipelineRenderingCreateInfo` with attachment formats).
Viewport and scissor are always dynamic state. Blend modes: `Opaque`, `Alpha`,
`PremultipliedAlpha`, `Additive`.

**`gfx::Descriptors`**: `DescriptorLayoutBuilder`, `DescriptorAllocator` (adds a new pool
whenever the current one is exhausted; optional per-set freeing) and `DescriptorWriter`
(batches `vkUpdateDescriptorSets`).

### 3.3 render2d

**`render2d::Camera2D`**: orthographic camera.

- World units are "pixels at zoom 1", **+X right, +Y down**.
- `position` (world point at screen centre), `zoom`, `rotation` (radians).
- `referenceHeight`: when non-zero, the camera always shows that many world units
  vertically, so the game looks the same at 720p, Steam Deck 800p or 4K.
- `screenToWorld` / `worldToScreen` for mouse picking, `visibleBounds` for culling.

**`render2d::SpriteBatch`**: batched textured quads (details in §6).
API: `begin(frame, viewProj)`, `draw(texture, Sprite)`, `draw(texture, pos, scale, rot,
color)`, `drawRect`, `drawRectOutline`, `drawLine`, `end()`. `UvRect` handles
atlas/sprite-sheet sub-rectangles and flipping. `stats()` reports sprites and draw calls.

---

## 4. Object ownership and lifetime

```mermaid
flowchart LR
    main["main()"] --> app["core::Application"]
    main --> g["Game (DemoGame)"]
    app --> win["Window"]
    app --> inp["Input"]
    app --> ctx["gfx::VulkanContext"]
    app --> ren["gfx::Renderer"]
    ren --> sc["Swapchain"]
    ren --> fr["Frame[N]: cmd pool, cmd buffer, imageAvailable, fence"]
    ctx --> vma["VMA allocator"]
    ctx --> tds["texture DescriptorAllocator + layout"]
    g --> sb["SpriteBatch"]
    g --> tex["Textures"]
    sb --> pipe["Pipeline"]
    sb --> bufs["index Buffer + vertex Buffer[N]"]
```

Rules:

- **All GPU wrappers are RAII and move-only** (`Buffer`, `Image`, `Texture`, `Pipeline`).
  They hold a pointer to the `VulkanContext` and destroy themselves in their destructor.
- In `main()`, the `Application` is declared before the `Game`, so the game (and every
  GPU object it owns) is destroyed first, while the device still exists.
- `Application::run()` calls `vkDeviceWaitIdle` before `onShutdown`, so destroying
  resources there or in the game's destructor is safe.
- **During gameplay**, destroying a resource that an in-flight frame may still use is an
  error. Call `renderer.waitIdle()` first, or keep resources alive for
  `framesInFlight` frames. (A deferred-deletion queue is a natural future addition.)
- In debug builds, VMA asserts at shutdown if any `Buffer`/`Image` leaked.

---

## 5. Main loop and frame lifecycle

### 5.1 CPU timeline

```mermaid
sequenceDiagram
    participant App as Application::run
    participant In as Input
    participant G as Game
    participant R as Renderer

    loop every frame
        App->>In: beginFrame() + SDL_PollEvent → handleEvent()
        Note over App: quit / resize / F11 handled here
        App->>App: dt = clamp(now - last, 0, 0.25 s)
        loop while accumulator ≥ fixedStep
            App->>G: onFixedUpdate(fixedStep)
        end
        App->>G: onUpdate(dt)
        App->>R: beginFrame()
        alt frame available
            App->>G: onResize() (if extent changed)
            App->>G: onPreRender(frame)
            App->>R: beginMainPass()
            App->>G: onRender(frame)
            App->>R: endMainPass() + endFrame()
        else minimised / swapchain out of date
            Note over App,R: skip rendering this frame
        end
    end
```

- **Fixed timestep with interpolation.** Simulation runs at `AppConfig::fixedTimestep`
  (1/60 s). `interpolationAlpha()` (0..1) tells how far the real time is between the last
  two fixed steps; render `mix(previousState, currentState, alpha)` for smooth motion on
  any refresh rate (144 Hz, 40 Hz Deck mode…). `DemoGame` does this for the player.
- `dt` is clamped to 0.25 s so a breakpoint or hitch doesn't cause a spiral of catch-up.
- When minimised the loop sleeps and resets the clock instead of rendering.

### 5.2 GPU frame (Renderer)

```mermaid
sequenceDiagram
    participant CPU
    participant GPU as Graphics queue
    participant PE as Presentation engine

    CPU->>CPU: wait fence[frame]
    CPU->>PE: vkAcquireNextImageKHR → signals imageAvailable[frame]
    CPU->>CPU: reset fence, reset command pool, begin cmd
    CPU->>CPU: barrier: swapchain image UNDEFINED → COLOR_ATTACHMENT
    CPU->>CPU: vkCmdBeginRendering (clear) … game draws … vkCmdEndRendering
    CPU->>CPU: barrier: COLOR_ATTACHMENT → PRESENT_SRC
    CPU->>GPU: vkQueueSubmit2(wait imageAvailable[frame], signal renderFinished[image], fence[frame])
    CPU->>PE: vkQueuePresentKHR(wait renderFinished[image])
```

**Synchronisation objects**

| Object | Count | Purpose |
|---|---|---|
| Fence `inFlight` | per frame in flight (2) | CPU waits before reusing that frame's command buffer and per-frame buffers |
| Semaphore `imageAvailable` | per frame in flight | acquire → rendering may start writing the image |
| Semaphore `renderFinished` | **per swapchain image** | rendering done → presentation may read the image |
| Command pool + buffer | per frame in flight | reset as a whole each frame (cheap) |

`renderFinished` is per *image*, not per frame, because the presentation engine may still
hold a semaphore after the frame's fence signals; tying it to the image guarantees it is
not reused too early.

**Swapchain recreation** is triggered by a pixel-size change event, `VK_SUBOPTIMAL_KHR`
/ `VK_ERROR_OUT_OF_DATE_KHR` from acquire or present, or a vsync toggle. It waits for the
device to go idle, then recreates the swapchain (passing the old one as `oldSwapchain`) and
the depth buffer, if any. Simple and robust; resizing is rare enough that the idle wait does
not matter.

`FrameContext` is what the game receives: `cmd`, `frameIndex` (index your per-frame
resources with it), `imageIndex`, `frameNumber` and `extent`.

---

## 6. Sprite batching

### 6.1 Data layout

```
Vertex (20 bytes)          Quad = 4 vertices, 6 indices
  vec2  position (world)     0 ─── 1        indices: 0 1 2  2 3 0
  vec2  uv                   │  ╲  │
  u32   color (RGBA8 sRGB)   3 ─── 2
```

- **Index buffer**: static, device-local, `uint32`, generated once for
  `maxSpritesPerFrame` quads (default 20 000).
- **Vertex buffers**: one per frame in flight, host-visible and persistently mapped. The
  CPU writes quads straight into mapped memory; no per-frame allocation or staging.
- **Push constant**: one `mat4` view-projection (64 bytes).
- **Descriptor set 0**: the texture's own set (`Texture::descriptorSet()`).

### 6.2 Batching rules

```mermaid
flowchart TD
    d["draw(texture, sprite)"] --> full{"buffer full?"}
    full -- yes --> warn["drop sprite, warn once per frame"]
    full -- no --> same{"same texture as current batch?"}
    same -- no --> flush["flush(): bind texture set, vkCmdDrawIndexed(pending quads)"] --> start["start new batch with this texture"]
    same -- yes --> write
    start --> write["write 4 vertices into mapped buffer"]
    e["end()"] --> f2["flush() remaining quads"] --> fl["flush written memory range (no-op if coherent)"]
```

- Consecutive sprites with the same texture become **one draw call**. Draw order is
  preserved, so layering = submission order. Group draws by texture, or use atlases, to
  minimise draw calls.
- `begin()/end()` may be used **multiple times per frame** (e.g. world camera, then HUD in
  screen space). Within a frame, later batches append after earlier ones in the same vertex
  buffer; the write cursor resets when `frame.frameNumber` changes.
- Sprite corners are transformed on the CPU (origin/pivot, rotation, size), so the shader
  stays trivial and any number of differently transformed sprites can share a draw.

### 6.3 Shaders

`sprite.vert` transforms by the view-projection and converts the vertex colour from sRGB
to linear. `sprite.frag` outputs `texture(uv) * color`. Pipeline: alpha blending, no depth,
no culling (so flipped/rotated sprites always render).

---

## 7. Coordinate systems and colour

**Screen/clip space.** Vulkan's clip space has +Y pointing **down**. `Camera2D` builds
`ortho(0, w, 0, h)`, which maps pixel (0,0) to the top-left without any flipping.

**Camera2D view matrix** (applied right to left to a world point):

```
view = T(viewport/2) · S(scale) · R(-rotation) · T(-position)
scale = zoom · (referenceHeight > 0 ? viewportHeight / referenceHeight : 1)
```

**Colour pipeline (linear workflow).**

```mermaid
flowchart LR
    A["Texture files (sRGB PNG)"] -->|"R8G8B8A8_SRGB: hardware decodes"| L["linear values in shader"]
    B["Tint colours in C++ (sRGB 0..1)"] -->|"sprite.vert: srgbToLinear()"| L
    L -->|"blend in linear space"| S["sRGB swapchain: hardware encodes"] --> D["Display"]
```

Rule of thumb: **author colours in sRGB** (as picked in an image editor); **shaders work
in linear**. Data textures (normal maps, masks) should use `TextureDesc::srgb = false`.

---

## 8. Build system

**CMake ≥ 3.22**, presets in `CMakePresets.json` (`debug`, `relwithdebinfo`, `release`,
`vs2022`, `vs2026`).

**Dependencies** are fetched by `cmake/Dependencies.cmake` with pinned versions:

| Library | Version | Use | Linked as |
|---|---|---|---|
| SDL3 | 3.4.16 | window, input, gamepads | static |
| Vulkan-Headers | SDK 1.4.350.0 | Vulkan types | headers |
| volk | SDK 1.4.350.0 | runtime Vulkan loader | compiled into `third_party` |
| VMA | 3.3.0 | GPU memory allocation | header (impl in `Implementations.cpp`) |
| GLM | 1.0.1 | vector/matrix math | headers |
| stb | pinned commit | image loading | header (impl in `Implementations.cpp`) |

Global definitions: `VK_NO_PROTOTYPES`, `GLM_FORCE_DEPTH_ZERO_TO_ONE`,
`GLM_FORCE_RADIANS`.

**Shaders** (`cmake/Shaders.cmake`): every `shaders/*.vert|*.frag|*.comp` is compiled to
`<exe dir>/shaders/<name>.spv` with `glslc` or `glslangValidator` (from the Vulkan SDK or
system packages). If neither exists, glslang is downloaded and built from source
automatically. `*.glsl` files in the folder are treated as includes and trigger rebuilds.

**Output layout**: `build/<preset>/bin/<Config>/` contains the executable, `shaders/` and
`assets/` (re-synced on every build). `cmake --install` produces a distributable folder.

---

## 9. Error handling, logging and debugging

- **Philosophy: fail fast and loud.** Unrecoverable conditions call `core::fatal()`,
  which logs, shows a message box (useful for players: "No GPU with Vulkan 1.3 support")
  and aborts.
- `VK_CHECK(expr)` wraps Vulkan calls and reports the result name, the expression and
  file:line.
- **Validation layers** are enabled in Debug builds when installed; override with the
  environment variable `GAME_VALIDATION=0/1`. Messages of warning level and above are
  routed to the log.
- **Debug names** are attached to buffers, images and views via `VK_EXT_debug_utils`, so
  RenderDoc / Nsight / validation messages show readable names.
- **Testing aid:** `GAME_MAX_FRAMES=N` exits after N rendered frames (smoke tests, CI).

---

## 10. Platform notes

| Platform | Notes |
|---|---|
| Windows | MSVC (VS 2022/2026), console in Debug, `WIN32` subsystem in the `release` preset (`GAME_WINDOWS_CONSOLE=OFF`). Vulkan SDK recommended for validation. |
| Linux | X11 and Wayland via SDL3 (dev packages needed at build time only). `$ORIGIN` rpath for shipping extra `.so` files. |
| Steam Deck | SteamOS + Mesa RADV (full Vulkan 1.3). 1280×800; controls exposed as a gamepad by Steam Input; borderless fullscreen under gamescope. Build in a container (distrobox) or the Steam Linux Runtime SDK for releases. |

---

## 11. Extending the framework

| You want to… | Where / how |
|---|---|
| Write your game | New `core::Game` subclass in `src/game/`, construct it in `main.cpp` |
| Add a shader | Drop `foo.vert` / `foo.frag` in `shaders/`, build a pipeline with `PipelineBuilder` using `core::shaderPath("foo.vert.spv")` |
| Add a new renderer (tilemap, text, particles on GPU) | New class in `render2d/` following `SpriteBatch`: own `Pipeline`, per-frame buffers indexed by `frame.frameIndex` |
| Render off-screen (post-processing, lighting) | Create an `Image` with `COLOR_ATTACHMENT \| SAMPLED` usage, render to it in `onPreRender`, sample it in the main pass |
| Change GPU features | Feature chain in `VulkanContext::createDevice()` |
| Upload data at runtime | `Buffer` with `CpuToGpu` per frame in flight, or `immediateSubmit` at load time |

---

## 12. Key design decisions

| Decision | Rationale | Trade-off |
|---|---|---|
| SDL3 as the platform layer | Best gamepad/Steam Deck support, X11 + Wayland, audio available later | One external dependency in the core |
| Vulkan 1.3 baseline | Dynamic rendering + sync2 remove most boilerplate; supported by all current desktop drivers and the Deck | Excludes very old GPUs/drivers |
| volk instead of linking the loader | Friendly error when Vulkan is missing; faster device-level calls | Function pointers instead of prototypes |
| VMA for memory | Correct, efficient suballocation without writing an allocator | Third-party header |
| One descriptor set per texture | Binding a texture is one call; no caches that can go stale | One set per texture (cheap); not bindless |
| CPU-side sprite transform | Trivial shaders, unlimited batching of mixed transforms | CPU cost per sprite (fine for tens of thousands) |
| Wait-idle on swapchain recreation | Simplest correct approach | Brief stall on resize only |
| Fixed-timestep simulation | Deterministic gameplay, frame-rate independent | Needs interpolation for smooth rendering |
| Identical `core/` + `gfx/` in 2D and 3D | Each project stands alone and is easy to understand | Changes must be copied between projects (or factored into a shared library later) |

---

## 13. Known limitations and roadmap

- No text rendering yet (candidates: stb_truetype atlas, MSDF fonts).
- No audio (SDL3 audio streams or miniaudio).
- No deferred resource destruction; destroy GPU objects only when idle (see §4).
- Single queue for graphics/transfer; uploads are blocking (`immediateSubmit`).
- No bindless textures; batches break on texture changes.
- No debug UI (Dear ImGui with `imgui_impl_sdl3` + `imgui_impl_vulkan` integrates cleanly).
