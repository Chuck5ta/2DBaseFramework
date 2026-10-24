# Game2D — Building for Windows, Linux and Steam Deck

> This guide covers setting up each platform, building, running, making a release build and
> getting it onto a Steam Deck. Commands are run from the project folder (the one containing
> `CMakeLists.txt`) unless stated otherwise.
> For how the code is structured, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## Contents

1. [Overview](#1-overview)
2. [Windows](#2-windows)
3. [Linux](#3-linux)
4. [Steam Deck](#4-steam-deck)
5. [Shipping on Steam](#5-shipping-on-steam)
6. [Troubleshooting](#6-troubleshooting)
7. [Reference](#7-reference)

---

## 1. Overview

The build uses **CMake** (≥ 3.22). On the first configure, CMake downloads the pinned
libraries (SDL3, Vulkan headers, volk, VMA, GLM, stb), so the first build needs **Git** and
an internet connection. Later builds work offline.

| | Windows | Linux | Steam Deck |
|---|---|---|---|
| Compiler | MSVC (Visual Studio 2022 / 2026) | GCC 11+ or Clang 14+ | GCC inside a container |
| Generator | Ninja or Visual Studio solution | Ninja | Ninja |
| Vulkan at build time | Headers are downloaded; SDK optional | Headers are downloaded | Headers are downloaded |
| Vulkan at run time | GPU driver | GPU driver (Mesa or NVIDIA) | Built in (Mesa RADV) |
| Shader compiler | Vulkan SDK `glslc`, or built automatically (needs Python 3) | `glslang-tools`, or built automatically | Built automatically if missing |

**Presets** (defined in `CMakePresets.json`):

| Preset | Use | Output folder |
|---|---|---|
| `debug` | Day-to-day development; validation layers on | `build/debug/bin/Debug/` |
| `relwithdebinfo` | Profiling: optimised, with debug symbols | `build/relwithdebinfo/bin/RelWithDebInfo/` |
| `release` | Shipping; no console window on Windows | `build/release/bin/Release/` |
| `vs2022` / `vs2026` | Generate a Visual Studio solution (Windows only) | `build/vs2022/bin/<Config>/` |

The output folder always contains everything needed to run:

```
Game2D(.exe)
shaders/*.spv     compiled from shaders/ on every build
assets/...        copied from assets/ on every build
```

The game finds these files relative to the executable, so it can be launched from anywhere.

> **Tip: share downloads between presets.** By default each build folder downloads its own
> copy of the libraries. To download once, add
> `-DFETCHCONTENT_BASE_DIR=C:/dev/deps` (Windows) or `-DFETCHCONTENT_BASE_DIR=$HOME/.cache/gamedeps`
> (Linux) to the first `cmake --preset …` command of each preset.

---

## 2. Windows

### 2.1 Install the tools (once)

1. **Visual Studio 2022 or 2026** (Community is fine): <https://visualstudio.microsoft.com/>
   In the installer, select the **Desktop development with C++** workload and make sure
   these components are ticked (they are by default):
   - *MSVC C++ x64/x86 build tools*
   - *C++ CMake tools for Windows* (provides CMake and Ninja)
   - *Windows SDK*
2. **Git for Windows**: <https://git-scm.com/download/win> (or tick *Git for Windows* in the
   Visual Studio installer). CMake uses it to download dependencies.
3. **Vulkan SDK** (recommended): <https://vulkan.lunarg.com/sdk/home#windows>
   Provides the validation layers, `glslc` and tools like *Vulkan Configurator*. The
   installer sets the `VULKAN_SDK` environment variable. **Restart Visual Studio** afterwards
   so it sees the variable. Without the SDK the project still builds (glslang is compiled from
   source on the first build, see the next item), but you get no validation messages.
4. **Python 3**, only if you skip the Vulkan SDK: <https://www.python.org/downloads/windows/>
   (tick *Add python.exe to PATH* in the installer). Building glslang from source runs a
   Python script. With the SDK installed, Python isn't needed.
5. An up-to-date **GPU driver** from NVIDIA, AMD or Intel.

### 2.2 Option A: Visual Studio, "Open Folder" (recommended)

1. Start Visual Studio → **Open a local folder** (or *File → Open → Folder…*) → select the
   `Game2D` folder.
2. Visual Studio reads `CMakePresets.json` and configures automatically. Watch the
   **Output** window (*Show output from: CMake*). The first configure downloads dependencies
   and can take a few minutes.
3. In the toolbar, pick the configuration preset **Debug (Ninja)**.
4. In the *Select Startup Item* dropdown (green ▶ button), choose **Game2D.exe**.
5. Press **F5** to build and debug, or **Ctrl+F5** to run without the debugger.

Log output appears in the console window that opens with the game and in Visual Studio's
**Output → Debug** pane.

To make a release build, switch the preset dropdown to **Release (Ninja)** and use
*Build → Build All*.

### 2.3 Option B: Classic Visual Studio solution (.sln)

Use this if you prefer the classic Solution Explorer or need features such as the
Visual Studio graphics/profiling tools tied to `.vcxproj` projects.

```powershell
cmake --preset vs2022        # or: cmake --preset vs2026
```

Then open `build\vs2022\Game2D.sln`. Game2D is already the startup project and its debugger
working directory is set. Choose *Debug* / *Release* in the toolbar and press **F5**.
Run `cmake --preset vs2022` again after adding or removing source files (or just build:
CMake re-globs `src/` automatically).

### 2.4 Option C: Command line

Open **Developer PowerShell for VS 2022** (or *x64 Native Tools Command Prompt*) from the Start
menu. A normal PowerShell won't find the compiler.

```powershell
cd F:\path\to\Game2D
cmake --preset debug
cmake --build --preset debug
.\build\debug\bin\Debug\Game2D.exe
```

Release:

```powershell
cmake --preset release
cmake --build --preset release
.\build\release\bin\Release\Game2D.exe
```

### 2.5 VS Code (optional)

1. Install the extensions **C/C++** and **CMake Tools** (Microsoft).
2. Start VS Code from a *Developer PowerShell* (`code .`) so MSVC is on the PATH.
3. *CMake: Select Configure Preset* → `debug`, then *CMake: Build* and *CMake: Debug*.

### 2.6 Release build and packaging

```powershell
cmake --preset release
cmake --build --preset release
cmake --install build\release --prefix dist\Game2D-windows
```

`dist\Game2D-windows` now contains `Game2D.exe`, `shaders\` and `assets\`. Zip it or upload
it to Steam as the Windows depot.

**Visual C++ runtime.** MSVC links the C++ runtime dynamically by default, so players need
the *Microsoft Visual C++ Redistributable (x64)*. Steam can install it for you (select it
as a redistributable in Steamworks). Alternatively, link the runtime statically so the
exe has no external dependency. Do this at configure time so every library uses the same
setting:

```powershell
cmake --preset release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
```

No Vulkan or SDL DLLs need to be shipped: SDL3 is linked statically and Vulkan is loaded
from the player's GPU driver at run time.

### 2.7 Validation layers on Windows

With the Vulkan SDK installed, Debug builds enable validation automatically and print any
API misuse to the log. You can force it either way with the environment variable
`GAME_VALIDATION=1` / `GAME_VALIDATION=0`. For extra checks (synchronization validation,
best practices), open **Vulkan Configurator** from the SDK, enable them there, and run the game
while the configurator is open.

---

## 3. Linux

### 3.1 Install the tools (once)

You need a C++20 compiler (GCC 11+ or Clang 14+), CMake ≥ 3.22, Ninja, Git, and the
development headers SDL3 uses for X11 and Wayland. The headers are only needed to **build**;
players don't need them.

**Ubuntu 22.04+ / Debian 12+ / Linux Mint / Pop!_OS**

```bash
sudo apt install build-essential cmake ninja-build git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl-dev \
  libdecor-0-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev \
  libasound2-dev libpulse-dev \
  glslang-tools vulkan-validationlayers vulkan-tools mesa-vulkan-drivers
```

**Fedora**

```bash
sudo dnf install gcc-c++ cmake ninja-build git pkgconf \
  libX11-devel libXext-devel libXrandr-devel libXcursor-devel libXfixes-devel libXi-devel \
  libXScrnSaver-devel libXtst-devel libxkbcommon-devel wayland-devel wayland-protocols-devel \
  mesa-libEGL-devel libdecor-devel libdrm-devel mesa-libgbm-devel systemd-devel dbus-devel \
  alsa-lib-devel pulseaudio-libs-devel \
  glslang vulkan-validation-layers vulkan-tools mesa-vulkan-drivers
```

**Arch Linux / Manjaro / EndeavourOS**

```bash
sudo pacman -S --needed base-devel cmake ninja git \
  libx11 libxext libxrandr libxcursor libxfixes libxi libxss libxtst libxkbcommon \
  wayland wayland-protocols libdecor libdrm mesa alsa-lib libpulse \
  glslang vulkan-validation-layers vulkan-tools vulkan-icd-loader
```

Plus a Vulkan driver: Mesa (`mesa-vulkan-drivers` / `vulkan-radeon` / `vulkan-intel`) for
AMD and Intel, or the proprietary NVIDIA driver. Check it works:

```bash
vulkaninfo --summary     # should list your GPU with apiVersion 1.3 or higher
```

### 3.2 Build and run

```bash
cd ~/path/to/Game2D
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/Debug/Game2D
```

Other presets work the same way (`relwithdebinfo`, `release`).

**IDEs:** VS Code (*CMake Tools*), CLion and Qt Creator all open `CMakePresets.json`
directly. Point the debugger at `build/debug/bin/Debug/Game2D`.

### 3.3 X11 vs Wayland

SDL3 picks the best backend automatically (Wayland when available, otherwise X11). To force
one, for example while debugging a platform-specific issue:

```bash
SDL_VIDEO_DRIVER=x11 ./build/debug/bin/Debug/Game2D
SDL_VIDEO_DRIVER=wayland ./build/debug/bin/Debug/Game2D
```

### 3.4 Release build and packaging

```bash
cmake --preset release
cmake --build --preset release
cmake --install build/release --prefix dist/Game2D-linux
tar -C dist -czf Game2D-linux.tar.gz Game2D-linux
```

The executable links only against system libraries (`libc`, `libstdc++`, `libm`,
`libgcc_s`); SDL3 is static and Vulkan is loaded at run time. Check with:

```bash
ldd dist/Game2D-linux/Game2D
```

**Portability:** a Linux binary only runs on systems whose `glibc`/`libstdc++` are at least
as new as the machine it was built on. A build from a brand-new distro may not start on an
older one. For anything you distribute, especially through Steam, build inside the Steam
Linux Runtime SDK container ([§4.3](#43-option-b-build-on-a-pc-with-the-steam-runtime-sdk-container)).
That produces a binary that runs on every Steam-supported Linux system, including the Deck.

---

## 4. Steam Deck

### 4.1 What you need to know

- The Deck runs **SteamOS 3** (Arch Linux based) with an AMD GPU and Mesa's **RADV** Vulkan
  driver, which fully supports everything this project uses.
- Screen: **1280×800**. In *Game Mode* games run fullscreen under the **gamescope**
  compositor. The built-in controls appear to the game as a standard **gamepad** via
  Steam Input.
- The system partition is **read-only**, and `pacman` changes are wiped by OS updates. Don't
  install build tools on the host; use a **container** instead. SteamOS 3.5 and later ship
  **distrobox** and **podman**, so no setup is needed.
- You run the game either through the **SteamOS Devkit Client** (best for iteration) or by
  adding it as a **Non-Steam Game**.

Choose a way to build:

| | Where you build | Good for |
|---|---|---|
| **Option A** | On the Deck, inside a container | Quick experiments, no PC needed |
| **Option B** | On your PC, inside the Steam Runtime SDK container | Recommended: portable binaries, same as for Steam releases |
| **Option C** | Windows build run through Proton | Quick check only; ship native |

### 4.2 Option A: Build on the Deck (distrobox)

1. Switch to **Desktop Mode** (Steam button → Power → Switch to Desktop).
2. Open **Konsole** (the terminal).
3. Create a build container from the Steam Linux Runtime SDK (one-time, a few hundred MB):

   ```bash
   distrobox create --name steamrt4 --image registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest
   distrobox enter steamrt4
   ```

   Your home folder is shared with the container, so the project and build output are
   accessible from both sides.

   > If the SDK image ever gives you trouble with distrobox, a plain Arch container works too:
   > `distrobox create --name gamedev --image archlinux:latest`, then install the Arch
   > packages from [§3.1](#31-install-the-tools-once). Binaries built there use Arch's newer
   > libraries, which is fine for testing on your own Deck but not for distribution.

4. Inside the container, get the project (copy it to e.g. `~/dev/Game2D` from a USB stick or a
   network share, or `git clone` your repository) and build a release:

   ```bash
   cd ~/dev/Game2D
   cmake --version          # needs 3.22 or newer
   cmake --preset release -B build/deck-release
   cmake --build build/deck-release
   ```

   If CMake stops because SDL3 is missing X11/Wayland headers, install them in the container
   with the Debian/Ubuntu package list from [§3.1](#31-install-the-tools-once)
   (`sudo apt install …`). If no shader compiler is found, glslang is built automatically
   (slower first build only).

5. Test it straight away from Desktop Mode (outside or inside the container):

   ```bash
   ~/dev/Game2D/build/deck-release/bin/Release/Game2D
   ```

6. Add it to Game Mode as described in [§4.5](#45-run-it-in-game-mode-as-a-non-steam-game).

### 4.3 Option B: Build on a PC with the Steam Runtime SDK container

Valve's **Steam Linux Runtime 4.0** ("steamrt4", Debian 13 based) SDK is the reference
build environment for native Linux games on Steam. A binary built inside it runs on the Deck
and on any Linux system with Steam. (The older 3.0 "sniper" runtime is Debian 11 based; use
steamrt4 for new projects.)

**On Linux** (with Docker or Podman installed):

```bash
cd ~/path/to/Game2D
podman run --rm -it -v "$PWD":/src:z -w /src \
  registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest \
  bash -c "cmake --preset release -B build/steamrt-release && cmake --build build/steamrt-release"
```

With Docker, replace `podman` by `docker` and add `--user "$(id -u):$(id -g)" -e HOME=/tmp`
so the build files aren't owned by root.

**On Windows**, use Docker Desktop (WSL 2 backend) from PowerShell:

```powershell
cd F:\path\to\Game2D
docker run --rm -it -v "${PWD}:/src" -w /src `
  registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk:latest `
  bash -c "cmake --preset release -B build/steamrt-release && cmake --build build/steamrt-release"
```

> Building on a Windows-mounted folder through Docker is noticeably slower than a native
> Linux filesystem. For frequent builds, keep a copy of the project inside WSL
> (`\\wsl$\…`) and run the same command there.

The result is in `build/steamrt-release/bin/Release/`. Using a separate build folder
(`-B build/steamrt-release`) keeps it apart from your normal Windows/Linux builds. Package it:

```bash
cmake --install build/steamrt-release --prefix dist/Game2D-steamrt
```

Then copy `dist/Game2D-steamrt` to the Deck ([§4.4](#44-get-the-build-onto-the-deck)).

> The SDK contains the compilers and build tools; if `cmake`, `ninja` or a header package is
> missing in the image version you pulled, install it inside the container with `apt`.
> Pin a specific image tag (instead of `latest`) once you start shipping, so builds are
> reproducible.

### 4.4 Get the build onto the Deck

**SteamOS Devkit Client (recommended for development)**

This is Valve's tool for pushing builds to a Deck over the network and launching them in
Game Mode. Details: <https://partner.steamgames.com/doc/steamdeck/loadgames>

1. On the Deck: *Settings → System → Enable Developer Mode*. Then *Settings → Developer →
   Pair new host* (Deck and PC must be on the same network).
2. On the PC: install **SteamOS Devkit Client** from your Steam library (*Software*) or via
   `steam://install/943760`. Start `windows-client\devkit-gui.exe` (Windows) or
   `linux-client/devkit-gui.sh` (Linux).
3. Find your Deck in the list, click **Register** and accept the pairing on the Deck. (Use
   *Add by IP* if it doesn't show up.)
4. In the **Title Upload** tab:
   - *Local Folder*: your packaged build folder (e.g. `dist/Game2D-steamrt`)
   - *Start Command*: `Game2D`
   - Leave **Steam Play unchecked** (it is for Windows builds running through Proton)
5. Click **Upload**. The game appears in the Deck's library as *Devkit Game: Game2D*.
   Re-upload after each build; the client only sends changed files.

**Copying manually** (USB stick, network share or `scp`):

```bash
# On the Deck (Desktop Mode, Konsole) — one-time, needed for scp/ssh access:
passwd                              # set a password for the 'deck' user
sudo systemctl enable --now sshd

# On your PC:
scp -r dist/Game2D-steamrt deck@steamdeck:~/Games/Game2D
```

Make sure the executable bit survived the copy (it is lost when copying via FAT/exFAT USB
sticks or Windows shares):

```bash
chmod +x ~/Games/Game2D/Game2D
```

### 4.5 Run it in Game Mode as a Non-Steam Game

1. In **Desktop Mode**, open **Steam**.
2. *Games → Add a Non-Steam Game to My Library… → Browse…*
3. Set the file type filter to *All Files*, select `~/Games/Game2D/Game2D`, then
   **Add Selected Programs**.
4. Return to **Game Mode** (desktop shortcut *Return to Gaming Mode*). The game is in your
   library under *Non-Steam*.

Notes:
- The game starts in a window; gamescope makes it fullscreen automatically. F11 isn't
  needed.
- Steam Input presents the controls as a gamepad. If buttons seem wrong, check the
  game's controller layout (*Steam button → Controller Settings*) is a gamepad template, not
  *Keyboard (WASD) and Mouse*.
- Log output: launch from Konsole in Desktop Mode to see it, or redirect it in the shortcut's
  launch options: `%command% > ~/Games/Game2D/log.txt 2>&1`.

### 4.6 Option C: Run the Windows build through Proton

For a quick check without a Linux build: copy the Windows release folder to the Deck, add
`Game2D.exe` as a Non-Steam Game, then *Properties → Compatibility → Force the use of a
specific Steam Play compatibility tool* → *Proton (latest)*. Vulkan is passed through
directly, so performance is close to native. For release, prefer the native build.

### 4.7 Things to test on the Deck

- 1280×800 resolution and text/UI legibility on a 7" screen.
- Performance with Steam's frame limiter (30/40/60 Hz) and the performance overlay
  (*… button → Performance*), and battery drain.
- Controller only: every menu reachable without mouse or keyboard.
- Suspend/resume (power button) while the game runs: the swapchain is recreated
  automatically.
- On-screen keyboard (Steam + X) if the game needs text input.

---

## 5. Shipping on Steam

A short checklist (full details in the Steamworks documentation:
<https://partner.steamgames.com/doc/sdk/uploading>):

1. Create **one depot per platform**: Windows (from §2.6) and Linux (from §4.3, the steamrt4
   SDK build). The Steam Deck uses the Linux depot.
2. Set the **launch options** for each OS: `Game2D.exe` on Windows, `Game2D` on Linux.
3. Under *Installation → Linux Runtime*, select **Steam Linux Runtime 4.0** for the Linux
   depot, matching the SDK you built with. Build 64-bit only.
4. Windows: add the *Visual C++ Redistributable* as a redistributable, unless you link
   the runtime statically (§2.6).
5. Upload with SteamPipe and test on a beta branch on Windows, a Linux PC and the Deck.
6. Request a **Steam Deck compatibility** review once controls, text size and performance
   are right.

---

## 6. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| CMake: *Could not find git* / download fails | Install Git and make sure it is on the PATH; check the internet connection for the first configure. |
| CMake error in `cmake/Dependencies.cmake` or `cmake/Shaders.cmake` (e.g. at `add_library`) right after adding a file in Visual Studio | Visual Studio's *Add → New Item* in the *CMake Targets View* inserted the new file into a CMake script. Remove the added path (e.g. `"../src/game/MyGame.h"`) from those scripts and from `CMakeLists.txt`, then *Project → Delete Cache and Reconfigure*. New files under `src/` need no CMake changes; add them via File Explorer or Solution Explorer's *Folder View*. |
| Windows: *'cl' / 'ninja' is not recognized*, or *No CMAKE_CXX_COMPILER could be found* | Use the **Developer PowerShell for VS** or open the folder in Visual Studio, not a plain terminal. |
| Linux: SDL3 configure error mentioning X11, Xcursor, Wayland… | Install the dev packages from §3.1. The error names the missing one. As a last resort you can disable a feature, e.g. `-DSDL_X11_XCURSOR=OFF`. |
| *No glslc/glslangValidator found - building glslang from source* | Not an error: no shader compiler was found, so one is built. Install the Vulkan SDK / `glslang-tools` to skip this. If the SDK *is* installed, restart Visual Studio (or your terminal) so it sees `VULKAN_SDK`, then delete the build folder and configure again. |
| CMake: *Could NOT find Python3* (while configuring glslang) | glslang is being built from source and needs Python 3. Install Python (on Windows tick *Add python.exe to PATH*), or install the Vulkan SDK so the source build isn't needed. |
| Game shows *No Vulkan driver found* | Install or update the GPU driver. On Linux, install `mesa-vulkan-drivers` (or your distro's equivalent) and check `vulkaninfo --summary`. |
| Game shows *No GPU with Vulkan 1.3 support* | The driver is too old or the GPU too old. Update the driver. Very old GPUs aren't supported by design. |
| Log says *Validation requested but VK_LAYER_KHRONOS_validation is not installed* | Harmless. Install the Vulkan SDK (Windows) or `vulkan-validationlayers` (Linux) to get validation. |
| *Failed to load shader '…/shaders/….spv'* | The shaders target didn't run, or the exe was copied without its `shaders/` folder. Rebuild, and ship the whole output folder (use `cmake --install`). |
| Steam Deck / Linux: *version `GLIBC_2.xx' not found* | The binary was built on a newer system than it runs on. Build in the steamrt4 SDK container (§4.3). |
| Steam Deck: *Permission denied* when launching | Executable bit lost during copying: `chmod +x Game2D`. |
| Steam Deck: controls act like a keyboard/mouse | Change the controller layout for the shortcut to a gamepad template (§4.5). |
| Windows: game window is slow or stutters only in Debug | Debug builds are unoptimised and run validation; compare with `relwithdebinfo` / `release`. |

---

## 7. Reference

**CMake options**

| Option | Default | Meaning |
|---|---|---|
| `CMAKE_BUILD_TYPE` | set by preset | `Debug`, `RelWithDebInfo`, `Release` |
| `GAME_WINDOWS_CONSOLE` | `ON` (`OFF` in `release` preset) | Show a console window on Windows |
| `FETCHCONTENT_BASE_DIR` | `<build dir>/_deps` | Where downloaded libraries are stored; share it between builds |
| `FETCHCONTENT_UPDATES_DISCONNECTED` | `OFF` | `ON` to skip update checks when re-configuring offline |
| `CMAKE_MSVC_RUNTIME_LIBRARY` | dynamic CRT | `MultiThreaded` for a statically linked C++ runtime (Windows) |

**Environment variables (at run time)**

| Variable | Effect |
|---|---|
| `GAME_VALIDATION=0/1` | Force Vulkan validation off/on (default: on in Debug builds) |
| `GAME_MAX_FRAMES=N` | Quit after N rendered frames (smoke tests, CI) |
| `SDL_VIDEO_DRIVER=x11/wayland` | Force the Linux windowing backend |
| `VK_ICD_FILENAMES` / `VK_DRIVER_FILES` | Force a specific Vulkan driver (advanced; e.g. testing with a software renderer) |

**Useful commands**

```bash
cmake --list-presets                 # show available presets
cmake --build --preset debug -j 8    # limit/raise parallel jobs
cmake --build --preset debug --target clean
cmake --fresh --preset debug         # re-configure from scratch (CMake 3.24+)
```

**Links**

- Vulkan SDK: <https://vulkan.lunarg.com/sdk/home>
- SDL3: <https://wiki.libsdl.org/SDL3/FrontPage>
- Steam Deck devkit / loading builds: <https://partner.steamgames.com/doc/steamdeck/loadgames>
- Steam Linux Runtime: <https://github.com/ValveSoftware/steam-runtime>
- Steam Linux Runtime 4.0 SDK: <https://gitlab.steamos.cloud/steamrt/steamrt4/sdk>
- distrobox on Steam Deck: <https://distrobox.it/posts/steamdeck_guide>
