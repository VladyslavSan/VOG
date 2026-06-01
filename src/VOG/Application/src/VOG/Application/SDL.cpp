#include "VOG/Application/SDL.hpp"

#include <bit>

#include "SDL3/SDL_platform_defines.h"
#include "SDL3/SDL_video.h"

namespace VOG::Application
{
Common::SurfaceHandle
SDLWindow::makeSurfaceHandles(const SDLWindow::Handle& window)
{
    const SDL_PropertiesID props = SDL_GetWindowProperties(window.get());

    auto propertyGetter = [props](const char* name, void* default_value) -> void*
    {
        return SDL_GetPointerProperty(props, name, default_value);
    };

#if defined(SDL_PLATFORM_WINDOWS)
    {
        const auto hwnd      = propertyGetter(SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        const auto hInstance = propertyGetter(SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
        return {
            .platform         = Common::SurfaceHandle::Platform::eWindows,
            .surfaceHandle    = std::bit_cast<std::uintptr_t>(hwnd),
            .additionalHandle = std::bit_cast<std::uintptr_t>(hInstance),
        };
    }
#elif defined(SDL_PLATFORM_APPLE)
    void* windowHandle = propertyGetter(
    #if defined(SDL_PLATFORM_MACOS)
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
    #else
        SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER
    #endif
        nullptr);
    return {
        .platform      = Common::SurfaceHandle::Platform::eApple,
        .surfaceHandle = std::bit_cast<std::uintptr_t>(windowHandle),
    };
#endif

    return {};
}
} // namespace VOG::Application
