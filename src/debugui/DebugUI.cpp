#include "debugui/DebugUI.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "core/Window.h"
#include "gfx/Pipeline.h"
#include "gfx/Renderer.h"
#include "gfx/Texture.h"
#include "gfx/VulkanContext.h"

#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <vector>

namespace debugui {

namespace {

void checkVk(VkResult result)
{
    if (result != VK_SUCCESS)
        LOG_ERROR("Dear ImGui Vulkan backend: VkResult %d", int(result));
}

bool isSrgb(VkFormat format)
{
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

// Must outlive the Vulkan backend (it keeps a pointer to the code).
std::vector<uint32_t> g_fragmentShader;

} // namespace

DebugUI::DebugUI(core::Window& window, gfx::VulkanContext& gpu, gfx::Renderer& renderer, const DebugUIDesc& desc)
    : gpu_(gpu)
    , desc_(desc)
    , visible_(desc.startVisible)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini next to wherever the game was started

    // Size: ImGui works in window coordinates. On Windows those are pixels, so
    // scale by the display's scale setting (e.g. 150%); on macOS/Wayland the
    // window is already in points and the backend handles the pixel density.
    SDL_Window* sdlWindow = window.handle();
    const float density = std::max(SDL_GetWindowPixelDensity(sdlWindow), 1.0f);
    const float scale = std::max(SDL_GetWindowDisplayScale(sdlWindow) / density, 1.0f) * desc.scale;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.92f;
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    // The project's own font if it's there (fonts are rasterised at whatever
    // size is needed, so it stays sharp at any scale); ImGui's built-in font otherwise.
    const std::string fontPath = core::assetPath("fonts/Roboto-Medium.ttf");
    ImFont* font = nullptr;
    if (SDL_GetPathInfo(fontPath.c_str(), nullptr))
        font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), desc.fontSize);
    if (!font)
        io.Fonts->AddFontDefault();
    style.FontSizeBase = desc.fontSize;

    ImGui_ImplSDL3_InitForVulkan(sdlWindow);

    VkFormat colorFormat = renderer.colorFormat();
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;   // copied by the backend
    rendering.depthAttachmentFormat = renderer.depthFormat(); // must match the main pass (3D has depth)

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = gpu.instance();
    info.PhysicalDevice = gpu.physicalDevice();
    info.Device = gpu.device();
    info.QueueFamily = gpu.graphicsFamily();
    info.Queue = gpu.graphicsQueue();
    // The backend creates its own descriptor pool: room for its font atlas
    // plus up to 64 game textures shown with ImGui::Image.
    info.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE + 64;
    info.MinImageCount = 2;
    info.ImageCount = std::max(renderer.framesInFlight(), 2u); // vertex buffers per frame in flight
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
    info.CheckVkResultFn = checkVk;
    // ImGui's colours are sRGB; our sRGB swapchain expects linear values. Our
    // version of its fragment shader converts them (see shaders/imgui.frag).
    if (isSrgb(colorFormat)) {
        g_fragmentShader = gfx::loadSpirv(core::shaderPath("imgui.frag.spv"));
        info.CustomShaderFragCreateInfo = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.CustomShaderFragCreateInfo.codeSize = g_fragmentShader.size() * sizeof(uint32_t);
        info.CustomShaderFragCreateInfo.pCode = g_fragmentShader.data();
    }
    if (!ImGui_ImplVulkan_Init(&info))
        core::fatal("Failed to initialise Dear ImGui's Vulkan backend");

    LOG_INFO("Debug UI: Dear ImGui %s (press the key below Esc to show it)", IMGUI_VERSION);
}

DebugUI::~DebugUI()
{
    gpu_.waitIdle();
    for (auto& [view, set] : textures_)
        ImGui_ImplVulkan_RemoveTexture(set);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    g_fragmentShader.clear();
}

bool DebugUI::wantsMouse() const
{
    return visible_ && ImGui::GetIO().WantCaptureMouse;
}

bool DebugUI::wantsKeyboard() const
{
    return visible_ && ImGui::GetIO().WantCaptureKeyboard;
}

ImTextureID DebugUI::texture(const gfx::Texture& texture)
{
    auto it = textures_.find(texture.view());
    if (it == textures_.end())
        it = textures_.emplace(texture.view(),
                               ImGui_ImplVulkan_AddTexture(texture.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)).first;
    return ImTextureID(it->second);
}

bool DebugUI::processEvent(const SDL_Event& event)
{
    // The toggle key belongs to the debug UI. Ignore it while typing into a text field.
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == desc_.toggleKey && !event.key.repeat &&
        !wantsKeyboard()) {
        toggle();
        return true;
    }

    // ImGui sees every event (so it never misses a key or button release)...
    ImGui_ImplSDL3_ProcessEvent(&event);

    // ...but the game doesn't get presses that ImGui is using. Releases always go
    // through, so a key held before the cursor moved over a window can't get stuck.
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_WHEEL:
        return wantsMouse();
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_TEXT_INPUT:
        return wantsKeyboard();
    default:
        return false;
    }
}

bool DebugUI::beginFrame()
{
    frameBuilt_ = false;
    if (!visible_)
        return false;
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return true;
}

void DebugUI::endFrame()
{
    ImGui::Render();
    frameBuilt_ = true;
}

void DebugUI::render(VkCommandBuffer cmd)
{
    if (!frameBuilt_)
        return;
    // Also uploads changed font atlas pages (on its own command buffer).
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    frameBuilt_ = false;
}

} // namespace debugui
