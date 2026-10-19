#include "gfx/Buffer.h"

#include "gfx/VulkanContext.h"

#include <cstring>
#include <utility>

namespace gfx {

Buffer::Buffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory,
               const char* debugName)
    : ctx_(&ctx), size_(size)
{
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    switch (memory) {
    case MemoryUsage::GpuOnly:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryUsage::CpuToGpu:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryUsage::GpuToCpu:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                          VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    VmaAllocationInfo result{};
    VK_CHECK(vmaCreateBuffer(ctx.allocator(), &info, &allocInfo, &buffer_, &allocation_, &result));
    mapped_ = result.pMappedData;

    if (debugName)
        ctx.setDebugName(VK_OBJECT_TYPE_BUFFER, uint64_t(buffer_), debugName);
}

Buffer::~Buffer()
{
    reset();
}

Buffer::Buffer(Buffer&& o) noexcept
    : ctx_(std::exchange(o.ctx_, nullptr)),
      buffer_(std::exchange(o.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(o.allocation_, VK_NULL_HANDLE)),
      size_(std::exchange(o.size_, 0)),
      mapped_(std::exchange(o.mapped_, nullptr))
{
}

Buffer& Buffer::operator=(Buffer&& o) noexcept
{
    if (this != &o) {
        reset();
        ctx_ = std::exchange(o.ctx_, nullptr);
        buffer_ = std::exchange(o.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(o.allocation_, VK_NULL_HANDLE);
        size_ = std::exchange(o.size_, 0);
        mapped_ = std::exchange(o.mapped_, nullptr);
    }
    return *this;
}

void Buffer::reset()
{
    if (buffer_)
        vmaDestroyBuffer(ctx_->allocator(), buffer_, allocation_);
    buffer_ = VK_NULL_HANDLE;
    allocation_ = VK_NULL_HANDLE;
    mapped_ = nullptr;
    size_ = 0;
}

Buffer Buffer::createWithData(VulkanContext& ctx, const void* data, VkDeviceSize size,
                              VkBufferUsageFlags usage, const char* debugName)
{
    Buffer staging(ctx, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::CpuToGpu, "staging");
    staging.write(data, size);

    Buffer buffer(ctx, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUsage::GpuOnly, debugName);
    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.handle(), buffer.handle(), 1, &region);
    });
    return buffer;
}

void Buffer::write(const void* data, VkDeviceSize size, VkDeviceSize offset)
{
    if (!mapped_)
        core::fatal("Buffer::write called on a buffer that is not host-visible");
    if (offset + size > size_)
        core::fatal("Buffer::write out of range (%llu + %llu > %llu)", (unsigned long long)offset,
                    (unsigned long long)size, (unsigned long long)size_);

    std::memcpy(static_cast<char*>(mapped_) + offset, data, size_t(size));
    flush(offset, size);
}

void Buffer::flush(VkDeviceSize offset, VkDeviceSize size)
{
    // No-op on host-coherent memory (almost always the case), required otherwise.
    if (allocation_ && size > 0)
        vmaFlushAllocation(ctx_->allocator(), allocation_, offset, size);
}

} // namespace gfx
