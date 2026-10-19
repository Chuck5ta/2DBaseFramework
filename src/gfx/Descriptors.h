#pragma once

#include "gfx/VkCommon.h"

#include <deque>
#include <initializer_list>
#include <vector>

namespace gfx {

// Builds a VkDescriptorSetLayout.
//
//   VkDescriptorSetLayout layout = DescriptorLayoutBuilder()
//       .add(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
//       .build(device);
class DescriptorLayoutBuilder {
public:
    DescriptorLayoutBuilder& add(uint32_t binding, VkDescriptorType type,
                                 VkShaderStageFlags stages, uint32_t count = 1);
    VkDescriptorSetLayout build(VkDevice device) const;

private:
    std::vector<VkDescriptorSetLayoutBinding> bindings_;
};

// Allocates descriptor sets from a list of pools, creating a new pool whenever
// the current one runs out, so callers never need to size pools up front.
class DescriptorAllocator {
public:
    struct PoolRatio {
        VkDescriptorType type;
        float descriptorsPerSet;
    };

    struct Allocation {
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
    };

    DescriptorAllocator() = default;
    ~DescriptorAllocator() { destroy(); }
    DescriptorAllocator(const DescriptorAllocator&) = delete;
    DescriptorAllocator& operator=(const DescriptorAllocator&) = delete;

    // allowFree: create pools with FREE_DESCRIPTOR_SET so individual sets can
    // be returned with free(). Otherwise only reset() reclaims memory.
    void init(VkDevice device, uint32_t setsPerPool, std::initializer_list<PoolRatio> ratios,
              bool allowFree);
    void destroy();

    Allocation allocate(VkDescriptorSetLayout layout);
    void free(const Allocation& allocation);
    void reset();

private:
    VkDescriptorPool createPool();

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t setsPerPool_ = 0;
    bool allowFree_ = false;
    std::vector<PoolRatio> ratios_;
    std::vector<VkDescriptorPool> pools_;
};

// Batches descriptor writes:
//
//   DescriptorWriter()
//       .buffer(0, ubo, sizeof(Uniforms), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
//       .image(1, view, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
//              VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
//       .update(device, set);
class DescriptorWriter {
public:
    DescriptorWriter& buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
                             VkDeviceSize offset, VkDescriptorType type);
    DescriptorWriter& image(uint32_t binding, VkImageView view, VkSampler sampler,
                            VkImageLayout layout, VkDescriptorType type);
    void update(VkDevice device, VkDescriptorSet set);

private:
    std::deque<VkDescriptorBufferInfo> bufferInfos_; // deque: stable addresses
    std::deque<VkDescriptorImageInfo> imageInfos_;
    std::vector<VkWriteDescriptorSet> writes_;
};

} // namespace gfx
