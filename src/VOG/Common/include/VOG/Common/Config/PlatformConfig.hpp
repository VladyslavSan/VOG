#pragma once

// clang-format off
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__) || defined(_WIN64)
    #define PLATFORM_WINDOWS
    #define PLATFORM_VIDEO_WINDOWS
#elif __APPLE__
    #include <TargetConditionals.h>
    #define PLATFORM_VIDEO_APPLE
    #if TARGET_IPHONE_SIMULATOR
        #define PLATFORM_APPLE_SIMULATOR
    #elif TARGET_OS_IPHONE
        #define PLATFORM_APPLE_IOS
    #elif TARGET_OS_MAC
        #define PLATFORM_APPLE_MACOS
    #else
    #   error "Unknown Apple platform"
    #endif
#elif __ANDROID__
    #define PLATFORM_ANDROID
    #define PLATFORM_VIDEO_ANDROID
#elif __linux__
    #define PLATFORM_LINUX

    // Linux video platforms may be supported multiple at a time
    
    #if __has_include(<wayland-client.h>)
        #define PLATFORM_VIDEO_LINUX_WAYLAND
        #define PLATFORM_VIDEO_LINUX_ANY_SUPPORTED 1
    #endif

    #if __has_include(<xcb/xcb.h>)
        #define PLATFORM_VIDEO_LINUX_XCB
        #undef PLATFORM_VIDEO_LINUX_ANY_SUPPORTED
        #define PLATFORM_VIDEO_LINUX_ANY_SUPPORTED 1
    #endif

    #if __has_include(<X11/Xlib.h>)
        #define PLATFORM_VIDEO_LINUX_XLIB
        #undef PLATFORM_VIDEO_LINUX_ANY_SUPPORTED
        #define PLATFORM_VIDEO_LINUX_ANY_SUPPORTED 1
    #endif

    #ifndef PLATFORM_VIDEO_LINUX_ANY_SUPPORTED
        #error "No video supported for linux"
    #endif

#elif __unix__ // all unices not caught above
    #define PLATFORM_UNIX
#elif defined(_POSIX_VERSION)
    #define PLATFORM_POSIX
#else
#   error "Unknown compiler"
#endif
// clang-format on