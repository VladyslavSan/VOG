#pragma once

namespace VOG::Engine
{
/** Per-frame inputs a Renderable may read while collecting its Draws. */
struct FrameContext
{
    /** Seconds elapsed since the renderer was constructed. */
    double timeSeconds = 0.0;
};
} // namespace VOG::Engine
