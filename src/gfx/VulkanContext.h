#pragma once

#include "gfx/Descriptors.h"
#include "gfx/VkCommon.h"

#include <functional>

namespace core { class Window; }

namespace gfx {

struct ContextDesc {
    const char* appName = "Game";
    bool validation = false; // enable VK_LAYER_KHRONOS_validation if installed
};

// Owns the Vulkan instance, the chosen GPU, the logical device, queues and the
// VMA memory allocator. Requires Vulkan 1.3 (dynamic rendering + sync2),
// which every current desktop driver and the Steam Deck (RADV) support.
class VulkanContext {
public:
    VulkanContext(core::Window& window, const ContextDesc& desc);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    VkInstance instance() const             { return instance_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const                 { return device_; }
    VkSurfaceKHR surface() const            { return surface_; }
    VmaAllocator allocator() const          { return allocator_; }

    VkQueue graphicsQueue() const     { return graphicsQueue_; }
    uint32_t graphicsFamily() const   { return graphicsFamily_; }
    VkQueue presentQueue() const      { return presentQueue_; }
    uint32_t presentFamily() const    { return presentFamily_; }

    const VkPhysicalDeviceProperties& properties() const { return properties_; }
    bool samplerAnisotropySupported() const { return anisotropySupported_; }
    bool wireframeSupported() const         { return wireframeSupported_; }

    // Records commands into a temporary command buffer, submits it and waits
    // for completion. Meant for loading-time work such as uploads; do not use
    // it every frame.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record);

    void waitIdle() const;

    // Names show up in validation messages, RenderDoc and Nsight.
    void setDebugName(VkObjectType type, uint64_t handle, const char* name) const;

    // Shared layout for "one combined image sampler at binding 0" sets, used
    // by Texture so every texture can carry its own ready-to-bind set.
    VkDescriptorSetLayout textureSetLayout() const { return textureSetLayout_; }
    DescriptorAllocator& textureDescriptors()      { return textureDescriptors_; }

private:
    void createInstance(core::Window& window, const ContextDesc& desc);
    void pickPhysicalDevice();
    void createDevice();
    void createAllocator();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = 0;
    uint32_t presentFamily_ = 0;

    VkPhysicalDeviceProperties properties_{};
    bool debugUtils_ = false;
    bool anisotropySupported_ = false;
    bool wireframeSupported_ = false;

    VkCommandPool uploadPool_ = VK_NULL_HANDLE;
    VkCommandBuffer uploadCmd_ = VK_NULL_HANDLE;
    VkFence uploadFence_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout textureSetLayout_ = VK_NULL_HANDLE;
    DescriptorAllocator textureDescriptors_;
};

} // namespace gfx
