#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cassert>
#define VMA_HEAVY_ASSERT(expr) assert(expr)
// #define VMA_USE_STL_CONTAINERS 1
// #define VMA_DEDICATED_ALLOCATION 0
// #define VMA_DEBUG_MARGIN 16
// #define VMA_DEBUG_DETECT_CORRUPTION 1
// #define VMA_DEBUG_INITIALIZE_ALLOCATIONS 1
// #define VMA_RECORDING_ENABLED 1
// #define VMA_DEBUG_MIN_BUFFER_IMAGE_GRANULARITY 256
// #define VMA_USE_STL_SHARED_MUTEX 0
// #define VMA_DEBUG_GLOBAL_MUTEX 1
// #define VMA_MEMORY_BUDGET 0

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#ifdef _WIN32
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #define VK_USE_PLATFORM_WIN32_KHR
#endif

#ifdef _MSVC_LANG
    #pragma warning(push, 4)
    #pragma warning(disable : 4127) // conditional expression is constant
    #pragma warning(disable : 4100) // unreferenced formal parameter
    #pragma warning(disable : 4189) // local variable is initialized but not referenced
    #pragma warning(disable : 4324) // structure was padded due to alignment specifier
#endif

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored                                                               \
        "-Wtautological-compare" // comparison of unsigned expression < 0 is always false
    #pragma clang diagnostic ignored "-Wunused-private-field"
    #pragma clang diagnostic ignored "-Wunused-parameter"
    #pragma clang diagnostic ignored "-Wmissing-field-initializers"
    #pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#include <vk_mem_alloc.h>

#ifdef __clang__
    #pragma clang diagnostic pop
#endif

#ifdef _MSVC_LANG
    #pragma warning(pop)
#endif
