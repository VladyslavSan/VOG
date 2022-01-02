#include "VOG/Application/SDL2.hpp"

namespace VOG::Application
{
void*
SDLWindow::getPlatformHandle() const
{
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    return mWmInfo.info.win.window;
#elif defined(SDL_VIDEO_DRIVER_COCOA)
    return (__bridge void*)mWmInfo.info.cocoa.window;
#elif defined(SDL_VIDEO_DRIVER_UIKIT)
    return (__bridge void*)mWmInfo.info.uikit.window;
#endif

    return nullptr;
}
} // namespace VOG::Application
