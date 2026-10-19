#pragma once

#include "gfx/Descriptors.h"
#include "gfx/Image.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>

namespace gfx {

struct TextureDesc {
    bool srgb = true;      // true for colour images; false for data (normal maps etc.)
    bool mipmaps = true;   // generate a full mip chain on upload
    VkFilter filter = VK_FILTER_LINEAR;                      // NEAREST for pixel art
    VkSamplerAddressMode wrap = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    const char* debugName = nullptr;
};

// A sampled RGBA8 texture: image + sampler + a ready-to-bind descriptor set
// (layout: VulkanContext::textureSetLayout(), binding 0 = combined sampler).
// Move-only RAII.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // `rgba` is width*height*4 bytes, rows top to bottom.
    static Texture fromPixels(VulkanContext& ctx, uint32_t width, uint32_t height,
                              const uint8_t* rgba, const TextureDesc& desc = {});
    // PNG, JPG, TGA, BMP, ... via stb_image. Aborts with a message if missing.
    static Texture fromFile(VulkanContext& ctx, const std::string& path, const TextureDesc& desc = {});
    // 1x1 texture; handy as a "no texture" default.
    static Texture solid(VulkanContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    VkDescriptorSet descriptorSet() const { return descriptor_.set; }
    VkImageView view() const              { return image_.view(); }
    VkSampler sampler() const             { return sampler_; }
    uint32_t width() const                { return image_.extent().width; }
    uint32_t height() const               { return image_.extent().height; }
    glm::vec2 size() const                { return {float(width()), float(height())}; }
    bool valid() const                    { return image_.valid(); }

    void reset();

private:
    VulkanContext* ctx_ = nullptr;
    Image image_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    DescriptorAllocator::Allocation descriptor_;
};

} // namespace gfx
