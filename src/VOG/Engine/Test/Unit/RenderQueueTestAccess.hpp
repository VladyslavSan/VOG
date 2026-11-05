#pragma once

#include "VOG/Engine/Renderer/DrawCheck.hpp"
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include <VOG/Engine/Renderer/Renderable.hpp>

#include <utility>

namespace VOG::Engine
{
/** Test-only window into the queue, befriended by RenderQueue. Defined here once: every test
 *  links into one executable. */
struct RenderQueueTestAccess
{
    /** A queue that checks its draws with @p check, as the renderer's does. */
    static RenderQueue
    make(Detail::DrawCheck& check)
    {
        return RenderQueue{check, {}};
    }

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

    /** Adds a draw to @p packet as the render thread finds it: checked by nobody. */
    static void
    addDraw(FramePacket& packet, Mesh mesh, Material material, const Math::Matrix4x4f& world)
    {
        packet.draws.push_back(
            {.mesh = std::move(mesh), .material = std::move(material), .world = world});
    }
};
} // namespace VOG::Engine
