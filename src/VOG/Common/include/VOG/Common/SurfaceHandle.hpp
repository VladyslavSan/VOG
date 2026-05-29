#pragma once

#include <cstdint>

namespace VOG::Common
{
struct SurfaceHandle
{
    /** Enum of platform types. */
    enum class Platform : std::uint8_t
    {
        eWindows,
        eApple,
        eNone,
    };

    /** Platform type. */
    Platform platform = Platform::eNone;

    /**
     * Most important handle used to create surface.
     * 1. Windows - HWND handle
     * 2. macOS - NSWindow or NSView
     */
    std::uintptr_t surfaceHandle = 0u;

    /** Additional optional handle.
     * 1. Windows - HINSTANCE handle.
     * 2. macOS - not used, will be 0.
     */
    std::uintptr_t additionalHandle = 0u;
};
} // namespace VOG::Common
