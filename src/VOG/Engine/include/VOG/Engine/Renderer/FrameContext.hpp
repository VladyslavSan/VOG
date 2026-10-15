#pragma once

namespace VOG::Engine
{
/** Per-frame inputs a Renderable may read in onDraw(). */
struct FrameContext
{
    /** Seconds since the application started; the caller of Renderer::submitFrame() sets it. */
    double timeSeconds = 0.0;
};
} // namespace VOG::Engine
