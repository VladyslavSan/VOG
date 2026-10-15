#pragma once

#include <cstdint>

namespace VOG::Engine
{
/** A size in pixels. Trivially copyable, so it fits a std::atomic; 8 bytes, so that is lock-free.
 */
struct Extent
{
    std::uint32_t width  = 0u;
    std::uint32_t height = 0u;

    bool operator==(const Extent&) const = default;
};
} // namespace VOG::Engine
