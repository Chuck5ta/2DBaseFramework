#include "gfx/Image.h"

#include "gfx/VulkanContext.h"

#include <utility>

namespace gfx {

void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                  uint32_t baseMip, uint32_t mipCount)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, baseMip, mipCount, 0, 1};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

Image::Image(VulkanContext& ctx, const ImageDesc& desc)
    : ctx_(&ctx), format_(desc.format), extent_{desc.width, desc.height}, mipLevels_(desc.mipLevels)
{
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = desc.format;
    info.extent = {desc.width, desc.height, 1};
    info.mipLevels = desc.mipLevels;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = desc.usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    // Render targets get their own memory block; helps some drivers.
    if (desc.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
        allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

    VK_CHECK(vmaCreateImage(ctx.allocator(), &info, &allocInfo, &image_, &allocation_, nullptr));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange = {desc.aspect, 0, desc.mipLevels, 0, 1};
    VK_CHECK(vkCreateImageView(ctx.device(), &viewInfo, nullptr, &view_));

    if (desc.debugName) {
        ctx.setDebugName(VK_OBJECT_TYPE_IMAGE, uint64_t(image_), desc.debugName);
        ctx.setDebugName(VK_OBJECT_TYPE_IMAGE_VIEW, uint64_t(view_), desc.debugName);
    }
}

Image::~Image()
{
    reset();
}

Image::Image(Image&& o) noexcept
    : ctx_(std::exchange(o.ctx_, nullptr)),
      image_(std::exchange(o.image_, VK_NULL_HANDLE)),
      view_(std::exchange(o.view_, VK_NULL_HANDLE)),
      allocation_(std::exchange(o.allocation_, VK_NULL_HANDLE)),
      format_(o.format_),
      extent_(o.extent_),
      mipLevels_(o.mipLevels_)
{
}

Image& Image::operator=(Image&& o) noexcept
{
    if (this != &o) {
        reset();
        ctx_ = std::exchange(o.ctx_, nullptr);
        image_ = std::exchange(o.image_, VK_NULL_HANDLE);
        view_ = std::exchange(o.view_, VK_NULL_HANDLE);
        allocation_ = std::exchange(o.allocation_, VK_NULL_HANDLE);
        format_ = o.format_;
        extent_ = o.extent_;
        mipLevels_ = o.mipLevels_;
    }
    return *this;
}

void Image::reset()
{
    if (view_)
        vkDestroyImageView(ctx_->device(), view_, nullptr);
    if (image_)
        vmaDestroyImage(ctx_->allocator(), image_, allocation_);
    view_ = VK_NULL_HANDLE;
    image_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
}

} // namespace gfx
