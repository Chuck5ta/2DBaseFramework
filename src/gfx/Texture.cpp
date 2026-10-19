#include "gfx/Texture.h"

#include "gfx/Buffer.h"
#include "gfx/VulkanContext.h"

#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gfx {

namespace {

bool supportsLinearBlit(VulkanContext& ctx, VkFormat format)
{
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice(), format, &props);
    const VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & needed) == needed;
}

} // namespace

Texture Texture::fromPixels(VulkanContext& ctx, uint32_t width, uint32_t height, const uint8_t* rgba,
                            const TextureDesc& desc)
{
    const VkFormat format = desc.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    uint32_t mipLevels = 1;
    if (desc.mipmaps && supportsLinearBlit(ctx, format))
        mipLevels = uint32_t(std::floor(std::log2(std::max(width, height)))) + 1;

    Texture tex;
    tex.ctx_ = &ctx;

    ImageDesc imageDesc;
    imageDesc.width = width;
    imageDesc.height = height;
    imageDesc.format = format;
    imageDesc.mipLevels = mipLevels;
    imageDesc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                      (mipLevels > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    imageDesc.debugName = desc.debugName;
    tex.image_ = Image(ctx, imageDesc);

    // --- Upload mip 0 and build the rest of the chain with blits ---
    const VkDeviceSize byteSize = VkDeviceSize(width) * height * 4;
    Buffer staging(ctx, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::CpuToGpu, "texture staging");
    staging.write(rgba, byteSize);

    const VkImage image = tex.image_.handle();
    ctx.immediateSubmit([&](VkCommandBuffer cmd) {
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.handle(), image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        int32_t mipW = int32_t(width), mipH = int32_t(height);
        for (uint32_t mip = 1; mip < mipLevels; ++mip) {
            // Previous level: TRANSFER_DST -> TRANSFER_SRC
            imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, mip - 1, 1);

            const int32_t nextW = std::max(mipW / 2, 1), nextH = std::max(mipH / 2, 1);
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, 1};
            blit.srcOffsets[1] = {mipW, mipH, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
            blit.dstOffsets[1] = {nextW, nextH, 1};
            vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
            mipW = nextW;
            mipH = nextH;
        }

        // Everything -> SHADER_READ_ONLY. Mips 0..n-2 are in TRANSFER_SRC,
        // the last one is still in TRANSFER_DST.
        if (mipLevels > 1)
            imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                         0, mipLevels - 1);
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                     mipLevels - 1, 1);
    });

    // --- Sampler ---
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = desc.filter;
    samplerInfo.minFilter = desc.filter;
    samplerInfo.mipmapMode = desc.filter == VK_FILTER_NEAREST ? VK_SAMPLER_MIPMAP_MODE_NEAREST
                                                              : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = desc.wrap;
    samplerInfo.addressModeV = desc.wrap;
    samplerInfo.addressModeW = desc.wrap;
    samplerInfo.maxLod = float(mipLevels);
    if (ctx.samplerAnisotropySupported() && desc.filter == VK_FILTER_LINEAR && mipLevels > 1) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(16.0f, ctx.properties().limits.maxSamplerAnisotropy);
    }
    VK_CHECK(vkCreateSampler(ctx.device(), &samplerInfo, nullptr, &tex.sampler_));

    // --- Descriptor set ---
    tex.descriptor_ = ctx.textureDescriptors().allocate(ctx.textureSetLayout());
    DescriptorWriter()
        .image(0, tex.image_.view(), tex.sampler_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
        .update(ctx.device(), tex.descriptor_.set);

    return tex;
}

Texture Texture::fromFile(VulkanContext& ctx, const std::string& path, const TextureDesc& desc)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels)
        core::fatal("Failed to load texture '%s': %s", path.c_str(), stbi_failure_reason());

    TextureDesc d = desc;
    if (!d.debugName)
        d.debugName = path.c_str();
    Texture tex = fromPixels(ctx, uint32_t(w), uint32_t(h), pixels, d);
    stbi_image_free(pixels);
    return tex;
}

Texture Texture::solid(VulkanContext& ctx, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    const uint8_t pixel[4] = {r, g, b, a};
    TextureDesc desc;
    desc.mipmaps = false;
    desc.filter = VK_FILTER_NEAREST;
    desc.debugName = "solid";
    return fromPixels(ctx, 1, 1, pixel, desc);
}

Texture::~Texture()
{
    reset();
}

Texture::Texture(Texture&& o) noexcept
    : ctx_(std::exchange(o.ctx_, nullptr)),
      image_(std::move(o.image_)),
      sampler_(std::exchange(o.sampler_, VK_NULL_HANDLE)),
      descriptor_(std::exchange(o.descriptor_, {}))
{
}

Texture& Texture::operator=(Texture&& o) noexcept
{
    if (this != &o) {
        reset();
        ctx_ = std::exchange(o.ctx_, nullptr);
        image_ = std::move(o.image_);
        sampler_ = std::exchange(o.sampler_, VK_NULL_HANDLE);
        descriptor_ = std::exchange(o.descriptor_, {});
    }
    return *this;
}

void Texture::reset()
{
    if (!ctx_)
        return;
    ctx_->textureDescriptors().free(descriptor_);
    descriptor_ = {};
    if (sampler_)
        vkDestroySampler(ctx_->device(), sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    image_.reset();
}

} // namespace gfx
