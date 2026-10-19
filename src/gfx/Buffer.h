#pragma once

#include "gfx/VkCommon.h"

namespace gfx {

class VulkanContext;

enum class MemoryUsage {
    GpuOnly,   // device-local; fill it with Buffer::createWithData / copies
    CpuToGpu,  // host-visible & persistently mapped; write every frame
    GpuToCpu,  // host-visible & mapped, cached; for readbacks
};

// A VkBuffer plus its VMA allocation. Move-only RAII.
//
// Destroying a buffer the GPU is still using is an error: call
// Renderer::waitIdle() first, or keep one buffer per frame in flight.
class Buffer {
public:
    Buffer() = default;
    Buffer(VulkanContext& ctx, VkDeviceSize size, VkBufferUsageFlags usage, MemoryUsage memory,
           const char* debugName = nullptr);
    ~Buffer();

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    // Creates a device-local buffer and uploads `data` through a staging
    // buffer (blocking). Ideal for static vertex/index data.
    static Buffer createWithData(VulkanContext& ctx, const void* data, VkDeviceSize size,
                                 VkBufferUsageFlags usage, const char* debugName = nullptr);

    // Copies into a mapped (CpuToGpu / GpuToCpu) buffer and flushes.
    void write(const void* data, VkDeviceSize size, VkDeviceSize offset = 0);
    // After writing through mapped() directly, flush the written range.
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    VkBuffer handle() const     { return buffer_; }
    VkDeviceSize size() const   { return size_; }
    void* mapped() const        { return mapped_; }
    bool valid() const          { return buffer_ != VK_NULL_HANDLE; }

    void reset(); // destroy now

private:
    VulkanContext* ctx_ = nullptr;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkDeviceSize size_ = 0;
    void* mapped_ = nullptr;
};

} // namespace gfx
