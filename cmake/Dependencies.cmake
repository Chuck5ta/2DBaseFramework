# Third-party dependencies, downloaded at configure time with FetchContent.
# Nothing needs to be installed system-wide except a C++20 compiler, CMake and
# (recommended) the Vulkan SDK for validation layers / shader compiler.
#
# All versions are pinned so builds are reproducible. Bump them deliberately.

include(FetchContent)

# Reuse downloads between build directories (e.g. debug + release):
#   cmake -DFETCHCONTENT_BASE_DIR=<some shared path> ...

# --- SDL3: window, input, gamepads (static, no runtime DLL to ship) --------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.4.16
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)

# --- Header-only / single-file libraries --------------------------------------
# SOURCE_SUBDIR points at a directory that doesn't exist so FetchContent only
# downloads them; we wire them up ourselves below.
FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        vulkan-sdk-1.4.350.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.4.350.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.3.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20
    SOURCE_SUBDIR  _download_only)

# miniaudio: audio output, mixing, decoding (WAV/FLAC/MP3, OGG via stb_vorbis), 3D sound.
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.23
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

# Dear ImGui: immediate-mode debug UI (windows, sliders, graphs). Built below
# with its SDL3 and Vulkan backends.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

# nlohmann/json: reading and writing JSON (Aseprite sprite sheets, settings, saves).
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _download_only)

FetchContent_MakeAvailable(SDL3 vulkan_headers volk vma glm stb miniaudio imgui json)

# --- One target that carries Vulkan headers, volk, VMA, GLM, stb, miniaudio, json ---
add_library(third_party STATIC ${volk_SOURCE_DIR}/volk.c)
target_include_directories(third_party SYSTEM PUBLIC
    ${vulkan_headers_SOURCE_DIR}/include
    ${volk_SOURCE_DIR}
    ${vma_SOURCE_DIR}/include
    ${glm_SOURCE_DIR}
    ${stb_SOURCE_DIR}
    ${miniaudio_SOURCE_DIR}
    ${json_SOURCE_DIR}/single_include)
target_compile_definitions(third_party PUBLIC
    VK_NO_PROTOTYPES                 # all Vulkan calls go through volk
    GLM_FORCE_DEPTH_ZERO_TO_ONE      # Vulkan clip space depth is [0, 1]
    GLM_FORCE_RADIANS
    GLM_ENABLE_EXPERIMENTAL)
find_package(Threads REQUIRED)
target_link_libraries(third_party PUBLIC Threads::Threads)
if(UNIX)
    # miniaudio loads ALSA/PulseAudio/PipeWire/JACK at run time, so no audio
    # dev packages are needed to build; it only needs dl, pthread and libm.
    target_link_libraries(third_party PUBLIC ${CMAKE_DL_LIBS} m)
endif()
set_target_properties(third_party PROPERTIES FOLDER "third_party")

# --- Dear ImGui (core + SDL3 and Vulkan backends) --------------------------------
# A separate library so its sources compile once, without the game's warning
# flags. The Vulkan backend calls Vulkan through volk, like the rest of the code.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(imgui PUBLIC SDL3::SDL3 third_party)
set_target_properties(imgui PROPERTIES FOLDER "third_party")
