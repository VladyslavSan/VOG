#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Engine
{
/** Per-frame inputs a Renderable may read while collecting its RenderItems. */
struct FrameContext
{
    /** Seconds elapsed since the renderer was constructed. */
    double timeSeconds = 0.0;

    /** Extent of the color target of this frame. */
    vk::Extent2D extent{};
};
} // namespace VOG::Engine
