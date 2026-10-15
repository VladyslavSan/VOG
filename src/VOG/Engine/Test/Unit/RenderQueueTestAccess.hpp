#pragma once

#include <VOG/Engine/Renderer/Renderable.hpp>

namespace VOG::Engine
{
/** Test-only window into the queue, befriended by RenderQueue. Defined here once: every test
 *  links into one executable. */
struct RenderQueueTestAccess
{
    static const std::vector<RenderQueue::Draw>&
    draws(const RenderQueue& queue)
    {
        return queue.mDraws;
    }

    static const CameraView&
    camera(const RenderQueue& queue)
    {
        return queue.mCamera;
    }
};
} // namespace VOG::Engine
