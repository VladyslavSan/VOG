#pragma once

#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#define VK_NO_PROTOTYPES

#define VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS

#include <VOG/Common/Config/PlatformConfig.hpp>

#ifdef PLATFORM_VIDEO_WINDOWS
    #define VK_USE_PLATFORM_WIN32_KHR
    #define NOMINMAX
#endif

#ifdef PLATFORM_VIDEO_APPLE
    // Generic metal extension name
    #define VK_USE_PLATFORM_METAL_EXT
    #ifdef TARGET_OS_MAC
        #define VK_USE_PLATFORM_MACOS_MVK
    #elif TARGET_OS_IPHONE
        #define VK_USE_PLATFORM_IOS_MVK
    #endif
#endif

#ifdef PLATFORM_VIDEO_ANDROID
    #define VK_USE_PLATFORM_ANDROID_KHR
#endif

#ifdef PLATFORM_VIDEO_LINUX_WAYLAND
    #define VK_USE_PLATFORM_WAYLAND_KHR
#endif

#ifdef PLATFORM_VIDEO_LINUX_XCB
    #define VK_USE_PLATFORM_XCB_KHR
#endif

#ifdef PLATFORM_VIDEO_LINUX_XLIB
    #define VK_USE_PLATFORM_XLIB_KHR
#endif

#include <vulkan/vulkan_raii.hpp>

#ifdef VK_USE_PLATFORM_XLIB_KHR
    // X11/Xlib.h defines several macros that conflict with C++ identifiers
    #ifdef None
        #undef None
    #endif
    #ifdef Bool
        #undef Bool
    #endif
    #ifdef True
        #undef True
    #endif
    #ifdef False
        #undef False
    #endif
    #ifdef Success
        #undef Success
    #endif
    #ifdef Status
        #undef Status
    #endif
#endif