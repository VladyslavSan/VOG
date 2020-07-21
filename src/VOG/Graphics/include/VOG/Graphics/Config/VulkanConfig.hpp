#pragma once

#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 1
#define VK_NO_PROTOTYPES

#include <VOG/Common/Config/PlatformConfig.hpp>

#ifdef PLATFORM_VIDEO_WINDOWS
#define VK_USE_PLATFORM_WIN32_KHR
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