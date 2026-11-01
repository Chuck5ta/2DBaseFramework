#pragma once

// In-game debug UI, built on Dear ImGui (https://github.com/ocornut/imgui).
//
// Press ` (the key below Esc) to show or hide it. While it is visible,
// Application calls Game::onDebugUI() every frame; put your ImGui windows there:
//
//   void MyGame::onDebugUI(core::Application& app) {
//       ImGui::Begin("Player");
//       ImGui::DragFloat2("Position", &playerPos_.x);
//       ImGui::SliderFloat("Speed", &speed_, 0.0f, 500.0f);
//       if (ImGui::Button("Respawn")) respawn();
//       ImGui::End();
//   }
//
// Including this header (it comes with core/Application.h) gives you the whole
// ImGui API. Call ImGui functions only from onDebugUI.
//
// It is drawn last, on top of everything, inside the main render pass.

#include "gfx/VkCommon.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_scancode.h>
#include <imgui.h>

#include <unordered_map>

namespace core { class Window; }
namespace gfx { class Renderer; class Texture; class VulkanContext; }

namespace debugui {

struct DebugUIDesc {
    // Created in debug builds only by default, so players never see it. Set it
    // to true (in main.cpp) to keep it in release builds too.
#ifdef NDEBUG
    bool enabled = false;
#else
    bool enabled = true;
#endif
    bool startVisible = false;
    SDL_Scancode toggleKey = SDL_SCANCODE_GRAVE; // the key below Esc; layout-independent
    float fontSize = 16.0f;  // in points; scaled for high-DPI screens automatically
    float scale = 1.0f;      // extra scale for everything (e.g. 1.5 on a small handheld screen)
    bool showEnginePanel = true; // Application's built-in "Engine" window
};

class DebugUI {
public:
    DebugUI(core::Window& window, gfx::VulkanContext& gpu, gfx::Renderer& renderer, const DebugUIDesc& desc);
    ~DebugUI();

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    void toggle() { visible_ = !visible_; }

    // True while the mouse is over a debug window / a text field has focus.
    // Application then keeps those events away from core::Input, so clicking
    // a button doesn't also shoot in the game.
    bool wantsMouse() const;
    bool wantsKeyboard() const;

    // Show a texture with ImGui::Image(app.debugUI().texture(tex), size).
    // The ImGui handle is created on first use and released with the DebugUI.
    ImTextureID texture(const gfx::Texture& texture);

    // --- Called by Application ---------------------------------------------------
    bool processEvent(const SDL_Event& event); // returns true if the game should not see it
    bool beginFrame();                          // false when hidden (nothing to build)
    void endFrame();
    void render(VkCommandBuffer cmd);           // inside the main pass, after the game

private:
    gfx::VulkanContext& gpu_;
    DebugUIDesc desc_;
    bool visible_ = false;
    bool frameBuilt_ = false;
    std::unordered_map<VkImageView, VkDescriptorSet> textures_;
};

} // namespace debugui
