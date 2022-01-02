#pragma once

#include <VOG/Common/Assert.hpp>

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
    SDLWindow(const char* title, int x, int y, int w, int h, Uint32 flags);

    Uint32 getWindowId() const;

    const SDL_SysWMinfo& getWMInfo() const;

    void* getPlatformHandle() const;

protected:
    Handle        mWindowHandle;
    Uint32        mWindowId;
    SDL_SysWMinfo mWmInfo;
};

inline SDLWindow::SDLWindow(const char* title, int x, int y, int w, int h, Uint32 flags)
    : mWindowHandle{SDL_CreateWindow(title, x, y, w, h, flags), SDL_DestroyWindow}
    , mWindowId{SDL_GetWindowID(mWindowHandle.get())}
{
    VOG_ASSERT_MSG(mWindowHandle, "Window handle should not be null.");

    SDL_VERSION(&mWmInfo.version);
    VOG_ASSERT_MSG(SDL_GetWindowWMInfo(mWindowHandle.get(), &mWmInfo) == SDL_TRUE,
                   "Failed to retrieve window info.");
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
} // namespace VOG::Application
