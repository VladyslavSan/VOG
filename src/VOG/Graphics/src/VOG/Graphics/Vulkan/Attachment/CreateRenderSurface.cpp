#include "VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#ifdef PLATFORM_VIDEO_WINDOWS
    #include <Windows.h>
#endif

#ifdef PLATFORM_APPLE_MACOS
    #import <AppKit/NSView.h>
    #import <AppKit/NSWindow.h>
#endif

#ifdef PLATFORM_APPLE_IOS
    #import <UIKit/UIWindow.h>
#endif

#ifdef PLATFORM_VIDEO_APPLE
    #import <QuartzCore/CAMetalLayer.h>
#endif

#ifdef PLATFORM_VIDEO_LINUX_WAYLAND
    #include <wayland-client.h>
#endif

#ifdef PLATFORM_VIDEO_LINUX_XLIB
    #include <X11/Xlib.h>
#endif

#ifdef PLATFORM_VIDEO_LINUX_XCB
    #include <xcb/xcb.h>
#endif

#include <bit>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
std::shared_ptr<vk::raii::SurfaceKHR>
CreateRenderSurface(const Instance&              instance,
                    const Common::SurfaceHandle& surface,
                    bool                         throwOnFail)
{
    std::shared_ptr<vk::raii::SurfaceKHR> surfaceHandle;

    switch (surface.platform)
    {
#ifdef PLATFORM_VIDEO_WINDOWS
    case Common::SurfaceHandle::Platform::eWindows:
    {
        void* windowHandle = std::bit_cast<void*>(surface.surfaceHandle);
        void* hinstance    = std::bit_cast<void*>(surface.additionalHandle);

        VOG_ASSERT(windowHandle != nullptr);
        VOG_ASSERT(hinstance != nullptr);

        vk::Win32SurfaceCreateInfoKHR createInfo{
            .hinstance = static_cast<HINSTANCE>(hinstance),
            .hwnd      = static_cast<HWND>(windowHandle),
        };

        surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(instance, createInfo);
        break;
    }
#endif

#ifdef PLATFORM_VIDEO_APPLE
    case Common::SurfaceHandle::Platform::eApple:
    {
        if (surface.surfaceHandle != 0u)
        {
    #ifdef TARGET_OS_MAC
            NSObject* object = (__bridge NSObject*)std::bit_cast<void*>(surface.surfaceHandle);
            NSView*   view   = nil;
            if ([object isKindOfClass:[NSWindow class]])
            {
                NSWindow* windowObject = (NSWindow*)object;
                view                   = windowObject.contentView;
            }
            else if ([object isKindOfClass:[NSView class]])
            {
                view = (NSView*)object;
            }

            if (view != nil)
            {
                view.wantsLayer = YES;
                view.layer      = [CAMetalLayer layer];

                vk::MetalSurfaceCreateInfoEXT createInfo{.pLayer = (CAMetalLayer*)view.layer};
                surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(instance, createInfo);
            }
    #elif defined(TARGET_OS_IPHONE)
    #endif
        }
        break;
    }
#endif

#ifdef PLATFORM_VIDEO_LINUX_WAYLAND
    case Common::SurfaceHandle::Platform::eLinuxWayland:
    {
        auto* display = std::bit_cast<wl_display*>(surface.additionalHandle);
        auto* wlSurf  = std::bit_cast<wl_surface*>(surface.surfaceHandle);
        VOG_ASSERT(display != nullptr);
        VOG_ASSERT(wlSurf != nullptr);

        vk::WaylandSurfaceCreateInfoKHR createInfo{
            .display = display,
            .surface = wlSurf,
        };
        surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(instance, createInfo);
        break;
    }
#endif

#ifdef PLATFORM_VIDEO_LINUX_XLIB
    case Common::SurfaceHandle::Platform::eLinuxXlib:
    {
        auto* display = std::bit_cast<Display*>(surface.additionalHandle);
        VOG_ASSERT(display != nullptr);

        vk::XlibSurfaceCreateInfoKHR createInfo{
            .dpy    = display,
            .window = static_cast<::Window>(surface.surfaceHandle),
        };
        surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(instance, createInfo);
        break;
    }
#endif

#ifdef PLATFORM_VIDEO_LINUX_XCB
    case Common::SurfaceHandle::Platform::eLinuxXcb:
    {
        auto* connection = std::bit_cast<xcb_connection_t*>(surface.additionalHandle);
        VOG_ASSERT(connection != nullptr);

        vk::XcbSurfaceCreateInfoKHR createInfo{
            .connection = connection,
            .window     = static_cast<xcb_window_t>(surface.surfaceHandle),
        };
        surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(instance, createInfo);
        break;
    }
#endif

    case Common::SurfaceHandle::Platform::eNone:
    default:
        break;
    }

    if (throwOnFail && (!surfaceHandle || !**surfaceHandle))
    {
        throw std::runtime_error("CreateSurface failed");
    }

    return surfaceHandle;
}
} // namespace VOG::Graphics::Vulkan
