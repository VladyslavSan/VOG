#include "VOG/Application/SDL2.hpp"

namespace VOG::Application
{
Common::JSONContainer
SDLWindow::makeSurfaceHandles(const SDL_SysWMinfo& wmInfo)
{
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    {
        void* window    = static_cast<void*>(wmInfo.info.win.window);
        void* hinstance = static_cast<void*>(wmInfo.info.win.hinstance);
        return {{"type", "windows"}, {"window", window}, {"hinstance", hinstance}};
    }
#elif defined(SDL_VIDEO_DRIVER_COCOA) || defined(SDL_VIDEO_DRIVER_UIKIT)
    {
        void* windowHandle =
#if defined(SDL_VIDEO_DRIVER_COCOA)
            (__bridge void*)wmInfo.info.cocoa.window;
#else
            (__bridge void*)wmInfo.info.uikit.window
#endif
        return {{"type", "apple"}, {"window", windowHandle}};
    }
#endif

    return nullptr;
}
} // namespace VOG::Application
