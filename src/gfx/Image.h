#pragma once

#include "gfx/VkCommon.h"

namespace gfx {

class VulkanContext;

// Records a single image layout transition / memory barrier (sync2).
void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                  uint32_t baseMip = 0, uint32_t mipCount = VK_REMAINING_MIP_LEVELS);

struct ImageDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t mipLevels = 1;
    const char* debugName = nullptr;
};

// A 2D VkImage + VMA allocation + a view covering all mips. Move-only RAII.
// Used for textures, depth buffers and render targets.
class Image {
public:
    Image() = default;
    Image(VulkanContext& ctx, const ImageDesc& desc);
    ~Image();

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;
    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    VkImage handle() const     { return image_; }
    VkImageView view() const   { return view_; }
    VkFormat format() const    { return format_; }
    VkExtent2D extent() const  { return extent_; }
    uint32_t mipLevels() const { return mipLevels_; }
    bool valid() const         { return image_ != VK_NULL_HANDLE; }

    void reset();

private:
    VulkanContext* ctx_ = nullptr;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};
    uint32_t mipLevels_ = 1;
};

} // namespace gfx
