#pragma once

#include "gfx/VkCommon.h"

#include <vector>

namespace gfx {

class VulkanContext;

// The swapchain and its per-image resources. Recreated on resize or when
// toggling vsync. Owned and driven by Renderer.
class Swapchain {
public:
    Swapchain(VulkanContext& ctx, uint32_t width, uint32_t height, bool vsync);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    // The device must be idle (Renderer guarantees this).
    void recreate(uint32_t width, uint32_t height, bool vsync);

    VkSwapchainKHR handle() const         { return swapchain_; }
    VkFormat format() const               { return format_; }
    VkExtent2D extent() const             { return extent_; }
    uint32_t imageCount() const           { return uint32_t(images_.size()); }
    VkImage image(uint32_t i) const       { return images_[i]; }
    VkImageView view(uint32_t i) const    { return views_[i]; }
    // Signalled when rendering to image i finishes; waited on by present.
    // One per swapchain image (not per frame in flight) so a semaphore is never
    // reused while the presentation engine may still hold it.
    VkSemaphore renderFinished(uint32_t i) const { return renderFinished_[i]; }

private:
    void create(uint32_t width, uint32_t height, bool vsync, VkSwapchainKHR old);
    void destroyImageResources();

    VulkanContext& ctx_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkSemaphore> renderFinished_;
};

} // namespace gfx
