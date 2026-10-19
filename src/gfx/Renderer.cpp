#include "gfx/Renderer.h"

#include "core/Window.h"
#include "gfx/VulkanContext.h"

namespace gfx {

namespace {

VkFormat findDepthFormat(VulkanContext& ctx)
{
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(ctx.physicalDevice(), f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return f;
    }
    core::fatal("No supported depth format found");
}

} // namespace

Renderer::Renderer(VulkanContext& ctx, core::Window& window, const RendererConfig& config)
    : ctx_(ctx), window_(window), config_(config)
{
    if (config_.framesInFlight == 0)
        config_.framesInFlight = 1;

    const glm::ivec2 size = window.pixelSize();
    swapchain_ = std::make_unique<Swapchain>(ctx, uint32_t(size.x), uint32_t(size.y), config_.vsync);

    if (config_.depthBuffer) {
        depthFormat_ = findDepthFormat(ctx);
        createDepthBuffer();
    }

    frames_.resize(config_.framesInFlight);
    for (Frame& f : frames_) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = ctx.graphicsFamily();
        VK_CHECK(vkCreateCommandPool(ctx.device(), &poolInfo, nullptr, &f.pool));

        VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdInfo.commandPool = f.pool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(ctx.device(), &cmdInfo, &f.cmd));

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(ctx.device(), &semInfo, nullptr, &f.imageAvailable));

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first wait returns immediately
        VK_CHECK(vkCreateFence(ctx.device(), &fenceInfo, nullptr, &f.inFlight));
    }
}

Renderer::~Renderer()
{
    waitIdle();
    for (Frame& f : frames_) {
        vkDestroyFence(ctx_.device(), f.inFlight, nullptr);
        vkDestroySemaphore(ctx_.device(), f.imageAvailable, nullptr);
        vkDestroyCommandPool(ctx_.device(), f.pool, nullptr);
    }
}

void Renderer::waitIdle()
{
    ctx_.waitIdle();
}

void Renderer::setVsync(bool vsync)
{
    if (vsync != config_.vsync) {
        config_.vsync = vsync;
        resizeRequested_ = true; // present mode is baked into the swapchain
    }
}

void Renderer::createDepthBuffer()
{
    ImageDesc desc;
    desc.width = swapchain_->extent().width;
    desc.height = swapchain_->extent().height;
    desc.format = depthFormat_;
    desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    desc.debugName = "depth buffer";
    depth_ = Image(ctx_, desc);
}

bool Renderer::recreateSwapchain()
{
    const glm::ivec2 size = window_.pixelSize();
    if (size.x == 0 || size.y == 0)
        return false; // minimised: try again next frame

    waitIdle();
    swapchain_->recreate(uint32_t(size.x), uint32_t(size.y), config_.vsync);
    if (config_.depthBuffer)
        createDepthBuffer();
    resizeRequested_ = false;
    return true;
}

FrameContext* Renderer::beginFrame()
{
    if (resizeRequested_ && !recreateSwapchain())
        return nullptr;

    Frame& f = frames_[currentFrame_];
    VK_CHECK(vkWaitForFences(ctx_.device(), 1, &f.inFlight, VK_TRUE, UINT64_MAX));

    uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(ctx_.device(), swapchain_->handle(), UINT64_MAX,
                                                   f.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        resizeRequested_ = true;
        return nullptr; // fence not reset, so the next wait still succeeds
    }
    if (acquire == VK_SUBOPTIMAL_KHR)
        resizeRequested_ = true; // still usable this frame; recreate next frame
    else if (acquire != VK_SUCCESS)
        VK_CHECK(acquire);

    VK_CHECK(vkResetFences(ctx_.device(), 1, &f.inFlight));
    VK_CHECK(vkResetCommandPool(ctx_.device(), f.pool, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(f.cmd, &begin));

    // Swapchain image: undefined -> colour attachment. The source stage
    // matches the acquire semaphore's wait stage in endFrame().
    imageBarrier(f.cmd, swapchain_->image(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);

    if (depth_.valid()) {
        // One depth buffer is shared by all frames in flight; this barrier
        // also orders against the previous frame's depth writes.
        imageBarrier(f.cmd, depth_.handle(), VK_IMAGE_ASPECT_DEPTH_BIT,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                     VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    }

    frame_.cmd = f.cmd;
    frame_.frameIndex = currentFrame_;
    frame_.imageIndex = imageIndex;
    frame_.frameNumber = frameNumber_;
    frame_.extent = swapchain_->extent();
    frameActive_ = true;
    return &frame_;
}

void Renderer::beginMainPass()
{
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = swapchain_->view(frame_.imageIndex);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{config_.clearColor.r, config_.clearColor.g, config_.clearColor.b,
                               config_.clearColor.a}};

    VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depth.imageView = depth_.view();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, frame_.extent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = depth_.valid() ? &depth : nullptr;
    vkCmdBeginRendering(frame_.cmd, &info);

    VkViewport viewport{0.0f, 0.0f, float(frame_.extent.width), float(frame_.extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, frame_.extent};
    vkCmdSetViewport(frame_.cmd, 0, 1, &viewport);
    vkCmdSetScissor(frame_.cmd, 0, 1, &scissor);
}

void Renderer::endMainPass()
{
    vkCmdEndRendering(frame_.cmd);
}

void Renderer::endFrame()
{
    if (!frameActive_)
        return;
    frameActive_ = false;

    Frame& f = frames_[currentFrame_];
    const VkSemaphore renderFinished = swapchain_->renderFinished(frame_.imageIndex);

    imageBarrier(f.cmd, swapchain_->image(frame_.imageIndex), VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);

    VK_CHECK(vkEndCommandBuffer(f.cmd));

    VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    wait.semaphore = f.imageAvailable;
    wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signal.semaphore = renderFinished;
    signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = f.cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &wait;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signal;
    VK_CHECK(vkQueueSubmit2(ctx_.graphicsQueue(), 1, &submit, f.inFlight));

    const VkSwapchainKHR swapchain = swapchain_->handle();
    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &frame_.imageIndex;

    const VkResult result = vkQueuePresentKHR(ctx_.presentQueue(), &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        resizeRequested_ = true;
    else if (result != VK_SUCCESS)
        VK_CHECK(result);

    currentFrame_ = (currentFrame_ + 1) % config_.framesInFlight;
    ++frameNumber_;
}

} // namespace gfx
