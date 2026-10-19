#pragma once

#include "gfx/Image.h"
#include "gfx/Swapchain.h"
#include "gfx/VkCommon.h"

#include <glm/vec4.hpp>

#include <memory>
#include <vector>

namespace core { class Window; }

namespace gfx {

class VulkanContext;

struct RendererConfig {
    bool vsync = true;
    bool depthBuffer = false;                 // 3D projects turn this on
    glm::vec4 clearColor{0.05f, 0.05f, 0.08f, 1.0f}; // linear colour
    uint32_t framesInFlight = 2;              // CPU may run this many frames ahead
};

// Everything a game needs to record one frame.
struct FrameContext {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t frameIndex = 0;   // 0..framesInFlight-1: index your per-frame resources with this
    uint32_t imageIndex = 0;   // swapchain image being rendered
    uint64_t frameNumber = 0;  // monotonically increasing
    VkExtent2D extent{};       // render target size in pixels
};

// Drives the per-frame loop:
//
//   beginFrame()     wait for this frame slot's fence, acquire a swapchain
//                    image, begin the command buffer
//   beginMainPass()  dynamic rendering into the swapchain (+ depth), clears,
//                    sets a full-screen viewport/scissor
//   ...game draws...
//   endMainPass()
//   endFrame()       transition to present, submit, present
//
// Application calls these for you; see Game::onPreRender/onRender.
class Renderer {
public:
    Renderer(VulkanContext& ctx, core::Window& window, const RendererConfig& config);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Returns nullptr when no frame can be rendered right now (minimised
    // window, swapchain being recreated); simply skip rendering then.
    FrameContext* beginFrame();
    void beginMainPass();
    void endMainPass();
    void endFrame();

    void requestResize() { resizeRequested_ = true; }
    void setVsync(bool vsync);
    bool vsync() const { return config_.vsync; }
    void setClearColor(const glm::vec4& linearColor) { config_.clearColor = linearColor; }

    VkFormat colorFormat() const { return swapchain_->format(); }
    VkFormat depthFormat() const { return depthFormat_; } // VK_FORMAT_UNDEFINED without depth
    VkExtent2D extent() const    { return swapchain_->extent(); }
    uint32_t framesInFlight() const { return config_.framesInFlight; }

    // Wait until the GPU has finished all submitted work. Call before
    // destroying resources that may still be in use.
    void waitIdle();

private:
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    bool recreateSwapchain();
    void createDepthBuffer();

    VulkanContext& ctx_;
    core::Window& window_;
    RendererConfig config_;

    std::unique_ptr<Swapchain> swapchain_;
    std::vector<Frame> frames_;
    Image depth_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;

    FrameContext frame_;
    uint32_t currentFrame_ = 0;
    uint64_t frameNumber_ = 0;
    bool resizeRequested_ = false;
    bool frameActive_ = false;
};

} // namespace gfx
