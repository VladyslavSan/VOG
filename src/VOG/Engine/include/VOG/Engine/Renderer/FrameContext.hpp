#pragma once

#include <algorithm>

namespace VOG::Engine
{
/** The longest step deltaSeconds reports: a long stall (a breakpoint, a window drag that held the
 *  loop) must not throw the simulation forward. */
inline constexpr float gMaxDeltaSeconds = 0.25f;

/** @p elapsedSeconds between two frames, limited to [0, gMaxDeltaSeconds]. */
constexpr float
frameDelta(double elapsedSeconds)
{
    return static_cast<float>(
        std::clamp(elapsedSeconds, 0.0, static_cast<double>(gMaxDeltaSeconds)));
}

/** Per-frame inputs a Renderable may read in onDraw() and a Behaviour in update(). */
struct FrameContext
{
    /** Seconds since the application started; the caller of Renderer::submitFrame() sets it. */
    double timeSeconds = 0.0;

    /**
     * Seconds since the previous frame of the same run(), limited to gMaxDeltaSeconds; 0 for the
     * first frame of a run(). The deltaSeconds of a run() sum to the time since its first frame,
     * minus whatever the limit cut; timeSeconds also counts the time before the run() and between
     * run()s.
     */
    float deltaSeconds = 0.0f;
};
} // namespace VOG::Engine
