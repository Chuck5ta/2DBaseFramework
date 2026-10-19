#pragma once

#include "gfx/VkCommon.h"

#include <string>
#include <vector>

namespace gfx {

class VulkanContext;

// Loads a compiled SPIR-V file (see the shaders/ folder and core::shaderPath).
std::vector<uint32_t> loadSpirv(const std::string& path);

// A graphics pipeline and its layout. Move-only RAII.
class Pipeline {
public:
    Pipeline() = default;
    Pipeline(VulkanContext& ctx, VkPipeline pipeline, VkPipelineLayout layout);
    ~Pipeline();

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    VkPipeline handle() const       { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    bool valid() const              { return pipeline_ != VK_NULL_HANDLE; }

    void bind(VkCommandBuffer cmd) const { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_); }
    void reset();

private:
    VulkanContext* ctx_ = nullptr;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

enum class BlendMode { Opaque, Alpha, PremultipliedAlpha, Additive };

// Fluent builder for graphics pipelines using dynamic rendering. Viewport
// and scissor are always dynamic state (the Renderer sets them each frame).
//
//   Pipeline p = PipelineBuilder()
//       .shaders(core::shaderPath("mesh.vert.spv"), core::shaderPath("mesh.frag.spv"))
//       .vertexBinding(0, sizeof(Vertex))
//       .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position))
//       .colorFormat(renderer.colorFormat())
//       .depthFormat(renderer.depthFormat())
//       .depthTest(true, true)
//       .build(ctx);
class PipelineBuilder {
public:
    PipelineBuilder();

    PipelineBuilder& shaders(const std::string& vertSpvPath, const std::string& fragSpvPath);
    PipelineBuilder& vertexBinding(uint32_t binding, uint32_t stride,
                                   VkVertexInputRate rate = VK_VERTEX_INPUT_RATE_VERTEX);
    PipelineBuilder& vertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset);
    PipelineBuilder& topology(VkPrimitiveTopology topology);
    PipelineBuilder& polygonMode(VkPolygonMode mode);
    PipelineBuilder& cullMode(VkCullModeFlags mode, VkFrontFace front = VK_FRONT_FACE_COUNTER_CLOCKWISE);
    PipelineBuilder& blend(BlendMode mode);
    PipelineBuilder& depthTest(bool test, bool write, VkCompareOp op = VK_COMPARE_OP_LESS_OR_EQUAL);
    PipelineBuilder& colorFormat(VkFormat format);
    PipelineBuilder& depthFormat(VkFormat format);
    PipelineBuilder& descriptorSetLayout(VkDescriptorSetLayout layout);
    PipelineBuilder& pushConstants(VkShaderStageFlags stages, uint32_t size, uint32_t offset = 0);

    Pipeline build(VulkanContext& ctx) const;

private:
    std::string vertPath_, fragPath_;
    std::vector<VkVertexInputBindingDescription> bindings_;
    std::vector<VkVertexInputAttributeDescription> attributes_;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly_{};
    VkPipelineRasterizationStateCreateInfo rasterizer_{};
    VkPipelineColorBlendAttachmentState blendAttachment_{};
    VkPipelineDepthStencilStateCreateInfo depthStencil_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<VkDescriptorSetLayout> setLayouts_;
    std::vector<VkPushConstantRange> pushConstants_;
};

} // namespace gfx
