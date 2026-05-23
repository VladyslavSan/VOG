#include "VOG/Application/SDL2.hpp"

namespace VOG::Application
{
Common::SurfaceHandle
SDLWindow::makeSurfaceHandles(const SDL_SysWMinfo& wmInfo)
{
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    {
        return {
            .platform         = Common::SurfaceHandle::Platform::eWindows,
            .surfaceHandle    = std::bit_cast<std::uintptr_t>(wmInfo.info.win.window),
            .additionalHandle = std::bit_cast<std::uintptr_t>(wmInfo.info.win.hinstance),
        };
    }
#elif defined(SDL_VIDEO_DRIVER_COCOA) || defined(SDL_VIDEO_DRIVER_UIKIT)
    {
        void* windowHandle =
#if defined(SDL_VIDEO_DRIVER_COCOA)
            (__bridge void*)wmInfo.info.cocoa.window;
#else
            (__bridge void*)wmInfo.info.uikit.window
#endif
        return {
            .platform      = Common::SurfaceHandle::Platform::eApple,
            .surfaceHandle = std::bit_cast<std::uintptr_t>(windowHandle),
        };
    }
#endif

    return {};
}
} // namespace VOG::Application
