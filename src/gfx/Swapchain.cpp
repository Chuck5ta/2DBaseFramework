#include "gfx/Swapchain.h"

#include "gfx/VulkanContext.h"

#include <algorithm>

namespace gfx {

namespace {

VkSurfaceFormatKHR chooseFormat(const std::vector<VkSurfaceFormatKHR>& formats)
{
    // Prefer an sRGB format so the hardware does linear -> sRGB conversion on
    // write. Shaders therefore output *linear* colour.
    for (const auto& f : formats)
        if ((f.format == VK_FORMAT_B8G8R8A8_SRGB || f.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    LOG_WARN("No sRGB swapchain format available; colours may look too dark");
    return formats[0];
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes, bool vsync)
{
    if (vsync)
        return VK_PRESENT_MODE_FIFO_KHR; // always supported, no tearing
    auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
    if (has(VK_PRESENT_MODE_MAILBOX_KHR))   return VK_PRESENT_MODE_MAILBOX_KHR;
    if (has(VK_PRESENT_MODE_IMMEDIATE_KHR)) return VK_PRESENT_MODE_IMMEDIATE_KHR;
    return VK_PRESENT_MODE_FIFO_KHR;
}

} // namespace

Swapchain::Swapchain(VulkanContext& ctx, uint32_t width, uint32_t height, bool vsync)
    : ctx_(ctx)
{
    create(width, height, vsync, VK_NULL_HANDLE);
}

Swapchain::~Swapchain()
{
    destroyImageResources();
    if (swapchain_)
        vkDestroySwapchainKHR(ctx_.device(), swapchain_, nullptr);
}

void Swapchain::recreate(uint32_t width, uint32_t height, bool vsync)
{
    destroyImageResources();
    VkSwapchainKHR old = swapchain_;
    create(width, height, vsync, old);
    vkDestroySwapchainKHR(ctx_.device(), old, nullptr);
}

void Swapchain::create(uint32_t width, uint32_t height, bool vsync, VkSwapchainKHR old)
{
    VkPhysicalDevice gpu = ctx_.physicalDevice();
    VkSurfaceKHR surface = ctx_.surface();

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &count, formats.data());

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, nullptr);
    std::vector<VkPresentModeKHR> modes(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &count, modes.data());

    const VkSurfaceFormatKHR surfaceFormat = chooseFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(modes, vsync);

    // currentExtent is 0xFFFFFFFF when the window manager lets us choose (Wayland).
    if (caps.currentExtent.width != UINT32_MAX) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Triple buffering: avoids stalls when a frame misses vsync.
    uint32_t imageCount = std::max(caps.minImageCount + 1, 3u);
    if (caps.maxImageCount > 0)
        imageCount = std::min(imageCount, caps.maxImageCount);

    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha & compositeAlpha)) {
        for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                              VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & a) { compositeAlpha = a; break; }
    }

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = compositeAlpha;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;

    const uint32_t families[] = {ctx_.graphicsFamily(), ctx_.presentFamily()};
    if (families[0] != families[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VK_CHECK(vkCreateSwapchainKHR(ctx_.device(), &info, nullptr, &swapchain_));
    format_ = surfaceFormat.format;

    vkGetSwapchainImagesKHR(ctx_.device(), swapchain_, &count, nullptr);
    images_.resize(count);
    vkGetSwapchainImagesKHR(ctx_.device(), swapchain_, &count, images_.data());

    views_.resize(count);
    renderFinished_.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(ctx_.device(), &viewInfo, nullptr, &views_[i]));

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(ctx_.device(), &semInfo, nullptr, &renderFinished_[i]));
    }

    static const char* modeNames[] = {"immediate", "mailbox", "fifo (vsync)", "fifo relaxed"};
    LOG_INFO("Swapchain %ux%u, %u images, present mode %s", extent_.width, extent_.height, count,
             presentMode <= 3 ? modeNames[presentMode] : "other");
}

void Swapchain::destroyImageResources()
{
    for (VkImageView v : views_)
        vkDestroyImageView(ctx_.device(), v, nullptr);
    for (VkSemaphore s : renderFinished_)
        vkDestroySemaphore(ctx_.device(), s, nullptr);
    views_.clear();
    renderFinished_.clear();
    images_.clear();
}

} // namespace gfx
