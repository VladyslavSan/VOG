#pragma once

#include "VOG/Engine/Renderer/DrawCheck.hpp"
#include "VOG/Engine/Renderer/FrameLoop.hpp"
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/GpuContext.hpp"
#include "VOG/Engine/Renderer/ResourceFactory.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include "VOG/Engine/Renderer/UploadQueue.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace VOG::Engine
{
/**
 * The components of a Renderer, in the order they are made: each is made from the ones above it.
 * The order protects the construction. The destruction does not lean on it, because ~Renderer
 * stops the render thread explicitly (FrameLoop::stop()) before any member dies, so nothing the
 * thread uses is gone while it runs.
 *
 * The members then die in reverse: after the thread come pending, pendingMutex, renderables,
 * drawCheck and resources, then the UploadQueue (its jobs that no iteration recorded drop their
 * staging and destination, and its pools wait for the copies in flight), the packet queue, the
 * shaders, and the GpuContext last.
 *
 * No waitIdle and no queue of deletions: what the GPU still reads is held by the command buffers
 * that read it.
 */
struct Renderer::Impl
{
    Impl(const Common::SurfaceHandle& surfaceHandle, const Config& config);

    /** Main thread. Calls onDraw() of every renderable with a queue, and returns the draws and
     *  the camera they set as a packet. */
    FramePacket makePacket(const FrameContext& frameContext);

    /** Main thread. Moves the pending renderables into renderables. */
    void adoptPendingRenderables();

    GpuContext       gpu;
    ShaderLibrary    shaders;
    FramePacketQueue packets;
    UploadQueue      uploads;
    ResourceFactory  resources;

    /** Main thread only: submitFrame() runs on one thread at a time. */
    Detail::DrawCheck                        drawCheck;
    std::vector<std::shared_ptr<Renderable>> renderables;

    /** Written by any thread, drained by the submitting thread. No callback runs under it. */
    std::mutex                              pendingMutex;
    std::deque<std::shared_ptr<Renderable>> pending;

    FrameLoop loop;
};
} // namespace VOG::Engine
