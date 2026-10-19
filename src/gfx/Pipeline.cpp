#include "gfx/Pipeline.h"

#include "gfx/VulkanContext.h"

#include <SDL3/SDL_iostream.h>

#include <cstring>
#include <iterator>
#include <utility>

namespace gfx {

std::vector<uint32_t> loadSpirv(const std::string& path)
{
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data)
        core::fatal("Failed to load shader '%s': %s\n\nWere the shaders compiled and copied next to "
                    "the executable?", path.c_str(), SDL_GetError());
    if (size == 0 || size % 4 != 0) {
        SDL_free(data);
        core::fatal("Shader '%s' is not valid SPIR-V (size %zu)", path.c_str(), size);
    }
    std::vector<uint32_t> code(size / 4);
    std::memcpy(code.data(), data, size);
    SDL_free(data);
    return code;
}

// ---------------------------------------------------------------------------
// Pipeline
// ---------------------------------------------------------------------------

Pipeline::Pipeline(VulkanContext& ctx, VkPipeline pipeline, VkPipelineLayout layout)
    : ctx_(&ctx), pipeline_(pipeline), layout_(layout)
{
}

Pipeline::~Pipeline()
{
    reset();
}

Pipeline::Pipeline(Pipeline&& o) noexcept
    : ctx_(std::exchange(o.ctx_, nullptr)),
      pipeline_(std::exchange(o.pipeline_, VK_NULL_HANDLE)),
      layout_(std::exchange(o.layout_, VK_NULL_HANDLE))
{
}

Pipeline& Pipeline::operator=(Pipeline&& o) noexcept
{
    if (this != &o) {
        reset();
        ctx_ = std::exchange(o.ctx_, nullptr);
        pipeline_ = std::exchange(o.pipeline_, VK_NULL_HANDLE);
        layout_ = std::exchange(o.layout_, VK_NULL_HANDLE);
    }
    return *this;
}

void Pipeline::reset()
{
    if (pipeline_) vkDestroyPipeline(ctx_->device(), pipeline_, nullptr);
    if (layout_)   vkDestroyPipelineLayout(ctx_->device(), layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// PipelineBuilder
// ---------------------------------------------------------------------------

PipelineBuilder::PipelineBuilder()
{
    inputAssembly_.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly_.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    rasterizer_.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer_.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer_.cullMode = VK_CULL_MODE_NONE;
    rasterizer_.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer_.lineWidth = 1.0f;

    depthStencil_.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil_.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil_.maxDepthBounds = 1.0f;

    blend(BlendMode::Opaque);
}

PipelineBuilder& PipelineBuilder::shaders(const std::string& vert, const std::string& frag)
{
    vertPath_ = vert;
    fragPath_ = frag;
    return *this;
}

PipelineBuilder& PipelineBuilder::vertexBinding(uint32_t binding, uint32_t stride, VkVertexInputRate rate)
{
    bindings_.push_back({binding, stride, rate});
    return *this;
}

PipelineBuilder& PipelineBuilder::vertexAttribute(uint32_t location, uint32_t binding, VkFormat format,
                                                  uint32_t offset)
{
    attributes_.push_back({location, binding, format, offset});
    return *this;
}

PipelineBuilder& PipelineBuilder::topology(VkPrimitiveTopology t)
{
    inputAssembly_.topology = t;
    return *this;
}

PipelineBuilder& PipelineBuilder::polygonMode(VkPolygonMode mode)
{
    rasterizer_.polygonMode = mode;
    return *this;
}

PipelineBuilder& PipelineBuilder::cullMode(VkCullModeFlags mode, VkFrontFace front)
{
    rasterizer_.cullMode = mode;
    rasterizer_.frontFace = front;
    return *this;
}

PipelineBuilder& PipelineBuilder::blend(BlendMode mode)
{
    VkPipelineColorBlendAttachmentState& b = blendAttachment_;
    b = {};
    b.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                       VK_COLOR_COMPONENT_A_BIT;
    b.colorBlendOp = VK_BLEND_OP_ADD;
    b.alphaBlendOp = VK_BLEND_OP_ADD;
    b.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

    switch (mode) {
    case BlendMode::Opaque:
        b.blendEnable = VK_FALSE;
        break;
    case BlendMode::Alpha:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::PremultipliedAlpha:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case BlendMode::Additive:
        b.blendEnable = VK_TRUE;
        b.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        b.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        b.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        break;
    }
    return *this;
}

PipelineBuilder& PipelineBuilder::depthTest(bool test, bool write, VkCompareOp op)
{
    depthStencil_.depthTestEnable = test ? VK_TRUE : VK_FALSE;
    depthStencil_.depthWriteEnable = write ? VK_TRUE : VK_FALSE;
    depthStencil_.depthCompareOp = op;
    return *this;
}

PipelineBuilder& PipelineBuilder::colorFormat(VkFormat format)
{
    colorFormat_ = format;
    return *this;
}

PipelineBuilder& PipelineBuilder::depthFormat(VkFormat format)
{
    depthFormat_ = format;
    return *this;
}

PipelineBuilder& PipelineBuilder::descriptorSetLayout(VkDescriptorSetLayout layout)
{
    setLayouts_.push_back(layout);
    return *this;
}

PipelineBuilder& PipelineBuilder::pushConstants(VkShaderStageFlags stages, uint32_t size, uint32_t offset)
{
    pushConstants_.push_back({stages, offset, size});
    return *this;
}

Pipeline PipelineBuilder::build(VulkanContext& ctx) const
{
    const VkDevice device = ctx.device();

    auto createModule = [&](const std::string& path) {
        const std::vector<uint32_t> code = loadSpirv(path);
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size() * sizeof(uint32_t);
        info.pCode = code.data();
        VkShaderModule module = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
        return module;
    };

    VkShaderModule vert = createModule(vertPath_);
    VkShaderModule frag = createModule(fragPath_);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = uint32_t(setLayouts_.size());
    layoutInfo.pSetLayouts = setLayouts_.data();
    layoutInfo.pushConstantRangeCount = uint32_t(pushConstants_.size());
    layoutInfo.pPushConstantRanges = pushConstants_.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout));

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = uint32_t(bindings_.size());
    vertexInput.pVertexBindingDescriptions = bindings_.data();
    vertexInput.vertexAttributeDescriptionCount = uint32_t(attributes_.size());
    vertexInput.pVertexAttributeDescriptions = attributes_.data();

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisample.minSampleShading = 1.0f;

    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blendState.attachmentCount = 1;
    blendState.pAttachments = &blendAttachment_;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = uint32_t(std::size(dynamicStates));
    dynamic.pDynamicStates = dynamicStates;

    // Dynamic rendering: attachment formats instead of a VkRenderPass.
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat_;
    rendering.depthAttachmentFormat = depthFormat_;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &inputAssembly_;
    info.pViewportState = &viewport;
    info.pRasterizationState = &rasterizer_;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil_;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamic;
    info.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));

    vkDestroyShaderModule(device, vert, nullptr);
    vkDestroyShaderModule(device, frag, nullptr);

    return Pipeline(ctx, pipeline, layout);
}

} // namespace gfx
