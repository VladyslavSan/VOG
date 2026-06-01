#include "VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp"

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

#ifdef PLATFORM_VIDEO_WINDOWS
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
    }
#elif defined(PLATFORM_VIDEO_APPLE)
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
    }
#endif

    if (throwOnFail && (!surfaceHandle || !**surfaceHandle))
    {
        throw std::runtime_error("CreateSurface failed");
    }

    return surfaceHandle;
}
} // namespace VOG::Graphics::Vulkan
