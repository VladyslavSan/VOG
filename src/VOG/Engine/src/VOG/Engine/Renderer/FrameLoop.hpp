#pragma once

#include "VOG/Engine/Renderer/DrawRecording.hpp"
#include "VOG/Engine/Renderer/PipelineTable.hpp"
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <atomic>
#include <exception>
#include <stop_token>
#include <thread>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(RenderTarget);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class FramePacketQueue;
class GpuContext;
class UploadQueue;
struct FramePacket;

/**
 * The render thread. It lives from the constructor to the destructor, and in every iteration it
 * waits for a packet or for uploads, copies what the UploadQueue holds, and, when it has a packet,
 * renders it: resolve the pipelines, prepare a frame slot, acquire an image, write the Frame
 * block, record the draws, submit and present.
 *
 * It owns the pipeline table, the extent it applied and the failure. Everything else it uses
 * belongs to the objects it is given, which outlive it: it is the last component of the
 * renderer, so it is joined before them.
 *
 * An exception on the render thread ends its work for good: the state becomes eFailed, and the
 * thread takes and drops packets until it is stopped, so the thread that submits never waits for
 * a consumer that is gone.
 */
class FrameLoop
{
public:
    /** Starts the thread. */
    FrameLoop(GpuContext& gpu, UploadQueue& uploads, FramePacketQueue& packets, Extent extent);

    /** Like stop(). */
    ~FrameLoop();

    FrameLoop(const FrameLoop&)            = delete;
    FrameLoop& operator=(const FrameLoop&) = delete;

    /**
     * Requests stop and joins the thread; does nothing if it was joined already. The stop is seen
     * within one acquire timeout and one frame, plus the fence waits of the frame slot, which have
     * no timeout (docs/SwapchainSync.md). Not while another thread is inside a member.
     */
    void stop();

    /** Any thread. */
    void resize(Extent extent) noexcept;

    /** Any thread. */
    [[nodiscard]] Renderer::State state() const noexcept;

    /** Any thread. The exception that ended the thread, or null while it runs; marks it
     *  collected. */
    [[nodiscard]] std::exception_ptr failure() noexcept;

    /** Any thread: whether failure() returned the exception. */
    [[nodiscard]] bool failureCollected() const noexcept;

    /** The token that ends the thread; a wait of the submitting thread uses it too. */
    [[nodiscard]] std::stop_token stopToken() const noexcept;

private:
    /** Lets RendererTest read the state that the thread leaves when it waits. */
    friend struct RendererTestAccess;

    void run(const std::stop_token& stopToken);

    /** Sets the ticket of every resource that a draw of @p packet uses and has not been copied. */
    static void markReferenced(const FramePacket& packet);

    /** Rebuilds the swapchain for @p requested; false if it is zero-sized. */
    bool applyExtent(Extent requested);

    /** One frame; an exception ends the thread. */
    void renderFrame(const FramePacket& packet);

    GpuContext&       mGpu;
    UploadQueue&      mUploads;
    FramePacketQueue& mPackets;

    /** Written by resize() on any thread. Extent is trivially copyable and 8 bytes, so the atomic
     *  is lock-free (FrameLoop.cpp asserts it). */
    std::atomic<Extent> mRequestedExtent;

    /** Render thread only: the last requested extent that was applied. Surfaces that report their
     *  own size ignore the request, so this is not necessarily the size of the images. */
    Extent mAppliedExtent;

    PipelineTable mPipelines;

    /**
     * The depth target that the frames share. Frames draw one after the other on the queue, and a
     * barrier at the start of each frame orders its depth writes after those of the frame before,
     * so one image does for all the frames in flight. It has the size of the image that the frame
     * acquired: the extent that was requested is not always the size of the images (surfaces that
     * report their own size ignore it). A new target replaces it when that size changes; a frame
     * that still draws into the old one holds it. Null until the first frame.
     */
    Graphics::Vulkan::RenderTargetPtr mDepthTarget;

    /** The draws of the frame being rendered. Scratch space, kept to reuse its capacity. */
    std::vector<ResolvedDraw> mResolved;

    /** Written once, before mState becomes eFailed. */
    std::exception_ptr mFailure;
    /** Release-stored by the thread after mFailure; acquire-loaded by the readers. */
    std::atomic<Renderer::State> mState            = Renderer::State::eRunning;
    std::atomic<bool>            mFailureCollected = false;

    /** Last member: joined before anything above is destroyed. */
    std::jthread mThread;
};
} // namespace VOG::Engine
