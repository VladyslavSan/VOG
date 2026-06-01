#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Common/SurfaceHandle.hpp>

#include <SDL3/SDL.h>

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
    static Common::SurfaceHandle makeSurfaceHandles(const SDLWindow::Handle& window);

    SDLWindow(const char* title, int w, int h, SDL_WindowFlags flags);

    Uint32 getWindowId() const;

    const Common::SurfaceHandle& getSurfaceHandle() const;

protected:
    Handle mWindowHandle;
    Uint32 mWindowId;

    Common::SurfaceHandle mSurfaceHandles;
};

inline SDLWindow::SDLWindow(const char* title, int w, int h, const SDL_WindowFlags flags)
    : mWindowHandle{SDL_CreateWindow(title, w, h, flags), SDL_DestroyWindow}
    , mWindowId{SDL_GetWindowID(mWindowHandle.get())}
{
    VOG_ASSERT_MSG(mWindowHandle, "Window handle should not be null.");

    mSurfaceHandles = SDLWindow::makeSurfaceHandles(mWindowHandle);
}

inline Uint32
SDLWindow::getWindowId() const
{
    return mWindowId;
}

inline const Common::SurfaceHandle&
SDLWindow::getSurfaceHandle() const
{
    return mSurfaceHandles;
}
} // namespace VOG::Application
