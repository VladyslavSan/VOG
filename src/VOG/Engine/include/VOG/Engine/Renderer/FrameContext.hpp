#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>

namespace VOG::Engine
{
/** Per-frame inputs a Renderable may read while collecting its RenderItems. */
struct FrameContext
{
    /** Seconds elapsed since the renderer was constructed. */
    double timeSeconds = 0.0;

    /** Extent of the color target of this frame. */
    vk::Extent2D extent{};

    /** Index of the frame, incremented once per submitted frame. */
    std::uint64_t frameIndex = 0u;
};
} // namespace VOG::Engine
