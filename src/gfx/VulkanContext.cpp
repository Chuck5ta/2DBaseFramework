#include "gfx/VulkanContext.h"

#include "core/Window.h"

#include <cstring>
#include <iterator>
#include <set>
#include <string>
#include <vector>

namespace gfx {

const char* vkResultToString(VkResult r)
{
    switch (r) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_ERROR_<unknown>";
    }
}

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data,
                                             void*)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        LOG_ERROR("[vulkan] %s", data->pMessage);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_WARN("[vulkan] %s", data->pMessage);
    else
        LOG_INFO("[vulkan] %s", data->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

bool hasLayer(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& l : layers)
        if (std::strcmp(l.layerName, name) == 0)
            return true;
    return false;
}

bool hasInstanceExtension(const char* name)
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}

bool hasDeviceExtension(VkPhysicalDevice gpu, const char* name)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, exts.data());
    for (const auto& e : exts)
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}

std::string versionString(uint32_t v)
{
    return std::to_string(VK_API_VERSION_MAJOR(v)) + "." + std::to_string(VK_API_VERSION_MINOR(v)) +
           "." + std::to_string(VK_API_VERSION_PATCH(v));
}

} // namespace

VulkanContext::VulkanContext(core::Window& window, const ContextDesc& desc)
{
    if (volkInitialize() != VK_SUCCESS)
        core::fatal("No Vulkan driver found.\n\nPlease install or update your graphics drivers.");

    createInstance(window, desc);
    surface_ = window.createSurface(instance_);
    pickPhysicalDevice();
    createDevice();
    createAllocator();

    // Command pool / fence for immediateSubmit().
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &uploadPool_));

    VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdInfo.commandPool = uploadPool_;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(device_, &cmdInfo, &uploadCmd_));

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(device_, &fenceInfo, nullptr, &uploadFence_));

    // Every Texture owns a set with this layout.
    textureSetLayout_ = DescriptorLayoutBuilder()
                            .add(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
                            .build(device_);
    textureDescriptors_.init(device_, 256, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0f}},
                             /*allowFree*/ true);
}

VulkanContext::~VulkanContext()
{
    if (device_)
        vkDeviceWaitIdle(device_);

    textureDescriptors_.destroy();
    if (textureSetLayout_) vkDestroyDescriptorSetLayout(device_, textureSetLayout_, nullptr);
    if (uploadFence_)      vkDestroyFence(device_, uploadFence_, nullptr);
    if (uploadPool_)       vkDestroyCommandPool(device_, uploadPool_, nullptr);

    if (allocator_) {
        // In debug builds VMA asserts here if any Buffer/Image was leaked.
        vmaDestroyAllocator(allocator_);
    }
    if (device_)         vkDestroyDevice(device_, nullptr);
    if (surface_)        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_) vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    if (instance_)       vkDestroyInstance(instance_, nullptr);
}

void VulkanContext::createInstance(core::Window& window, const ContextDesc& desc)
{
    const uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_3)
        core::fatal("Vulkan 1.3 is required, but the installed Vulkan loader only supports %s.\n\n"
                    "Please update your graphics drivers.",
                    versionString(loaderVersion).c_str());

    std::vector<const char*> extensions = window.requiredVulkanExtensions();
    std::vector<const char*> layers;

    if (desc.validation) {
        if (hasLayer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            LOG_INFO("Vulkan validation layer enabled");
        } else {
            LOG_WARN("Validation requested but VK_LAYER_KHRONOS_validation is not installed "
                     "(install the Vulkan SDK, or the vulkan-validation-layers package on Linux)");
        }
    }
    // Debug utils gives readable object names in tools (RenderDoc etc.) even
    // without validation, so enable it whenever it exists.
    if (hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        debugUtils_ = true;
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = desc.appName;
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.pEngineName = "custom";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = uint32_t(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = uint32_t(layers.size());
    info.ppEnabledLayerNames = layers.data();

    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = debugMessengerInfo();
    const bool useMessenger = debugUtils_ && !layers.empty();
    if (useMessenger)
        info.pNext = &messengerInfo; // also report problems in vkCreate/DestroyInstance

    VK_CHECK(vkCreateInstance(&info, nullptr, &instance_));
    volkLoadInstance(instance_);

    if (useMessenger)
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &messengerInfo, nullptr, &debugMessenger_));
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(instance_, &count, gpus.data());

    int bestScore = -1;
    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpu, &props);

        auto reject = [&](const char* why) {
            LOG_INFO("GPU '%s' skipped: %s", props.deviceName, why);
        };

        if (props.apiVersion < VK_API_VERSION_1_3) { reject("Vulkan 1.3 not supported"); continue; }
        if (!hasDeviceExtension(gpu, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) { reject("no swapchain"); continue; }

        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;
        vkGetPhysicalDeviceFeatures2(gpu, &f2);
        if (!f13.dynamicRendering || !f13.synchronization2) {
            reject("dynamicRendering/synchronization2 missing");
            continue;
        }

        // Queue families: prefer one family that does graphics + present.
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());

        int graphics = -1, present = -1;
        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 canPresent = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &canPresent);
            const bool canGraphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (canGraphics && canPresent) { graphics = present = int(i); break; }
            if (canGraphics && graphics < 0) graphics = int(i);
            if (canPresent && present < 0)   present = int(i);
        }
        if (graphics < 0 || present < 0) { reject("no graphics/present queue"); continue; }

        int score = 1;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 100;
        if (graphics == present) score += 10;

        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = gpu;
            graphicsFamily_ = uint32_t(graphics);
            presentFamily_ = uint32_t(present);
            properties_ = props;
            anisotropySupported_ = f2.features.samplerAnisotropy == VK_TRUE;
            wireframeSupported_ = f2.features.fillModeNonSolid == VK_TRUE;
        }
    }

    if (!physicalDevice_)
        core::fatal("No GPU with Vulkan 1.3 support was found.\n\nPlease update your graphics drivers.");

    LOG_INFO("GPU: %s (Vulkan %s, driver 0x%x)", properties_.deviceName,
             versionString(properties_.apiVersion).c_str(), properties_.driverVersion);
}

void VulkanContext::createDevice()
{
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : std::set<uint32_t>{graphicsFamily_, presentFamily_}) {
        VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        queueInfos.push_back(q);
    }

    // Feature chain: 1.0 -> 1.2 -> 1.3. Add what your game needs here.
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    f13.dynamicRendering = VK_TRUE; // no VkRenderPass / VkFramebuffer objects
    f13.synchronization2 = VK_TRUE; // simpler, less error-prone barriers

    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;

    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &f12;
    features.features.samplerAnisotropy = anisotropySupported_ ? VK_TRUE : VK_FALSE;
    features.features.fillModeNonSolid = wireframeSupported_ ? VK_TRUE : VK_FALSE;

    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features;
    info.queueCreateInfoCount = uint32_t(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    info.enabledExtensionCount = uint32_t(std::size(extensions));
    info.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateDevice(physicalDevice_, &info, nullptr, &device_));
    volkLoadDevice(device_); // direct device function pointers (skips loader dispatch)

    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

void VulkanContext::createAllocator()
{
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.physicalDevice = physicalDevice_;
    info.device = device_;
    info.instance = instance_;
    info.pVulkanFunctions = &functions;
    VK_CHECK(vmaCreateAllocator(&info, &allocator_));
}

void VulkanContext::immediateSubmit(const std::function<void(VkCommandBuffer)>& record)
{
    VK_CHECK(vkResetCommandBuffer(uploadCmd_, 0));

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(uploadCmd_, &begin));
    record(uploadCmd_);
    VK_CHECK(vkEndCommandBuffer(uploadCmd_));

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = uploadCmd_;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;

    VK_CHECK(vkQueueSubmit2(graphicsQueue_, 1, &submit, uploadFence_));
    VK_CHECK(vkWaitForFences(device_, 1, &uploadFence_, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(device_, 1, &uploadFence_));
}

void VulkanContext::waitIdle() const
{
    vkDeviceWaitIdle(device_);
}

void VulkanContext::setDebugName(VkObjectType type, uint64_t handle, const char* name) const
{
    if (!debugUtils_ || !name || !vkSetDebugUtilsObjectNameEXT)
        return;
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

} // namespace gfx
