#include "VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>

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

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
std::shared_ptr<vk::raii::SurfaceKHR>
CreateRenderSurface(GraphicsProviderPtr          graphicsProvider,
                    const Common::JSONContainer& parameters,
                    bool                         throwOnFail)
{
    std::shared_ptr<vk::raii::SurfaceKHR> surfaceHandle;

#if defined(PLATFORM_VIDEO_WINDOWS)
    {
        void* windowHandle = parameters["window"].getOr<void*>(nullptr);
        void* instance     = parameters["insntace"].getOr<void*>(nullptr);

        if (windowHandle != nullptr)
        {
            vk::Win32SurfaceCreateInfoKHR createInfo{};
            createInfo.setHwnd(reinterpret_cast<HWND>(windowHandle))
                .setHinstance(GetModuleHandleA(0));

            surfaceHandle =
                std::make_shared<vk::raii::SurfaceKHR>(graphicsProvider->getInstance(), createInfo);
        }
    }
#elif defined(PLATFORM_VIDEO_APPLE)
    {
        void* windowHandle = parameters["window"].getOr<void*>(nullptr);
        if (windowHandle != nullptr)
        {
#if defined(TARGET_OS_MAC)
            NSObject* object = (__bridge NSObject*)windowHandle;
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
                surfaceHandle = std::make_shared<vk::raii::SurfaceKHR>(
                    graphicsProvider->getInstance(), createInfo);
            }
#elif defined(TARGET_OS_IPHONE)
#endif
        }
    }
#endif

    if (throwOnFail && !surfaceHandle)
        throw std::runtime_error("CreateSurface failed");

    return surfaceHandle;
}
} // namespace VOG::Graphics::Vulkan
