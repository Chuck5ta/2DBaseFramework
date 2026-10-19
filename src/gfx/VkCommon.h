#pragma once

// Single include for Vulkan in this project.
//
// VK_NO_PROTOTYPES is defined by the build, so the regular Vulkan headers only
// declare types; every Vulkan function is a pointer loaded at runtime by volk.
// This means the game does not link against vulkan-1.dll / libvulkan.so and
// can show a friendly error if no Vulkan driver is installed.

#include <vulkan/vulkan.h>
#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/Log.h"

namespace gfx {
const char* vkResultToString(VkResult result);
}

#define VK_CHECK(expr)                                                                   \
    do {                                                                                 \
        const VkResult vkCheckResult_ = (expr);                                          \
        if (vkCheckResult_ != VK_SUCCESS)                                                \
            ::core::fatal("Vulkan call failed: %s\n%s\n(%s:%d)",                         \
                          ::gfx::vkResultToString(vkCheckResult_), #expr, __FILE__,      \
                          __LINE__);                                                     \
    } while (0)
