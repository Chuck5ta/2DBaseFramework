#include "gfx/Descriptors.h"

#include <algorithm>

namespace gfx {

// ---------------------------------------------------------------------------
// DescriptorLayoutBuilder
// ---------------------------------------------------------------------------

DescriptorLayoutBuilder& DescriptorLayoutBuilder::add(uint32_t binding, VkDescriptorType type,
                                                      VkShaderStageFlags stages, uint32_t count)
{
    VkDescriptorSetLayoutBinding b{};
    b.binding = binding;
    b.descriptorType = type;
    b.descriptorCount = count;
    b.stageFlags = stages;
    bindings_.push_back(b);
    return *this;
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device) const
{
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = uint32_t(bindings_.size());
    info.pBindings = bindings_.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

// ---------------------------------------------------------------------------
// DescriptorAllocator
// ---------------------------------------------------------------------------

void DescriptorAllocator::init(VkDevice device, uint32_t setsPerPool,
                               std::initializer_list<PoolRatio> ratios, bool allowFree)
{
    device_ = device;
    setsPerPool_ = setsPerPool;
    ratios_.assign(ratios.begin(), ratios.end());
    allowFree_ = allowFree;
    pools_.push_back(createPool());
}

void DescriptorAllocator::destroy()
{
    for (VkDescriptorPool pool : pools_)
        vkDestroyDescriptorPool(device_, pool, nullptr);
    pools_.clear();
}

VkDescriptorPool DescriptorAllocator::createPool()
{
    std::vector<VkDescriptorPoolSize> sizes;
    for (const PoolRatio& r : ratios_)
        sizes.push_back({r.type, std::max(1u, uint32_t(r.descriptorsPerSet * float(setsPerPool_)))});

    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.flags = allowFree_ ? VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT : 0;
    info.maxSets = setsPerPool_;
    info.poolSizeCount = uint32_t(sizes.size());
    info.pPoolSizes = sizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorPool(device_, &info, nullptr, &pool));
    return pool;
}

DescriptorAllocator::Allocation DescriptorAllocator::allocate(VkDescriptorSetLayout layout)
{
    VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorSetCount = 1;
    info.pSetLayouts = &layout;

    Allocation result;
    info.descriptorPool = pools_.back();
    VkResult r = vkAllocateDescriptorSets(device_, &info, &result.set);

    if (r == VK_ERROR_OUT_OF_POOL_MEMORY || r == VK_ERROR_FRAGMENTED_POOL) {
        pools_.push_back(createPool());
        info.descriptorPool = pools_.back();
        r = vkAllocateDescriptorSets(device_, &info, &result.set);
    }
    VK_CHECK(r);
    result.pool = info.descriptorPool;
    return result;
}

void DescriptorAllocator::free(const Allocation& allocation)
{
    if (allocation.set == VK_NULL_HANDLE)
        return;
    if (!allowFree_) {
        LOG_WARN("DescriptorAllocator::free called on an allocator created without allowFree");
        return;
    }
    vkFreeDescriptorSets(device_, allocation.pool, 1, &allocation.set);
}

void DescriptorAllocator::reset()
{
    for (VkDescriptorPool pool : pools_)
        VK_CHECK(vkResetDescriptorPool(device_, pool, 0));
}

// ---------------------------------------------------------------------------
// DescriptorWriter
// ---------------------------------------------------------------------------

DescriptorWriter& DescriptorWriter::buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size,
                                           VkDeviceSize offset, VkDescriptorType type)
{
    VkDescriptorBufferInfo& info = bufferInfos_.emplace_back(VkDescriptorBufferInfo{buffer, offset, size});

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = type;
    w.pBufferInfo = &info;
    writes_.push_back(w);
    return *this;
}

DescriptorWriter& DescriptorWriter::image(uint32_t binding, VkImageView view, VkSampler sampler,
                                          VkImageLayout layout, VkDescriptorType type)
{
    VkDescriptorImageInfo& info = imageInfos_.emplace_back(VkDescriptorImageInfo{sampler, view, layout});

    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = type;
    w.pImageInfo = &info;
    writes_.push_back(w);
    return *this;
}

void DescriptorWriter::update(VkDevice device, VkDescriptorSet set)
{
    for (VkWriteDescriptorSet& w : writes_)
        w.dstSet = set;
    vkUpdateDescriptorSets(device, uint32_t(writes_.size()), writes_.data(), 0, nullptr);
}

} // namespace gfx
