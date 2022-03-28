#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Common/JSONContainer.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <memory>

namespace VOG::Application
{
struct SDLHandle
{
public:
    SDLHandle(Uint32 flags)
        : flags{flags}
    {
        SDL_InitSubSystem(flags);
    }

    ~SDLHandle() { SDL_Quit(); }

    const Uint32 flags;
};

class SDLWindow
{
    using Handle = std::unique_ptr<SDL_Window, void (*)(SDL_Window*)>;

public:
    static Common::JSONContainer makeSurfaceHandles(const SDL_SysWMinfo&);

    SDLWindow(const char* title, int x, int y, int w, int h, Uint32 flags);

    Uint32 getWindowId() const;

    const SDL_SysWMinfo& getWMInfo() const;

    const Common::JSONContainer& getSurfaceHandle() const;

protected:
    Handle        mWindowHandle;
    Uint32        mWindowId;
    SDL_SysWMinfo mWmInfo;

    Common::JSONContainer mSurfaceHandles;
};

inline SDLWindow::SDLWindow(const char* title, int x, int y, int w, int h, Uint32 flags)
    : mWindowHandle{SDL_CreateWindow(title, x, y, w, h, flags), SDL_DestroyWindow}
    , mWindowId{SDL_GetWindowID(mWindowHandle.get())}
{
    VOG_ASSERT_MSG(mWindowHandle, "Window handle should not be null.");

    SDL_VERSION(&mWmInfo.version);
    VOG_ASSERT_MSG(SDL_GetWindowWMInfo(mWindowHandle.get(), &mWmInfo) == SDL_TRUE,
                   "Failed to retrieve window info.");

    mSurfaceHandles = SDLWindow::makeSurfaceHandles(mWmInfo);
}

inline Uint32
SDLWindow::getWindowId() const
{
    return mWindowId;
}

inline const SDL_SysWMinfo&
SDLWindow::getWMInfo() const
{
    return mWmInfo;
}
inline const Common::JSONContainer&
SDLWindow::getSurfaceHandle() const
{
    return mSurfaceHandles;
}
} // namespace VOG::Application
