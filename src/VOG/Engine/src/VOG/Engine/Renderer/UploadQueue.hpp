#pragma once

#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <variant>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Buffer);
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(FencePool);
VOG_DECLARE_PTR(Texture);
VOG_DECLARE_PTR(TimelineSemaphore);
class CommandBufferRecorder;
class Queue;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class FramePacketQueue;
struct UploadTicket;

/** The bytes of a mesh or of a texture, waiting for the render thread to copy them from staging
 *  into the buffer or image, which are preferably device-local. */
struct UploadJob
{
    Graphics::Vulkan::BufferPtr                                             staging;
    std::variant<Graphics::Vulkan::BufferPtr, Graphics::Vulkan::TexturePtr> destination;
    /** Shared with the handle of the resource. */
    std::shared_ptr<UploadTicket> ticket;
};

/**
 * The copies of meshes and textures from staging to the GPU, from the thread that creates the
 * resource to the render thread.
 *
 * Any thread pushes a job. The render thread, in every iteration, takes the jobs, drops those that
 * nobody can draw any more, records the others on the transfer queue and submits them. It records
 * the jobs of resources that a draw of the iteration uses (the ones with `ticket->referenced`)
 * all, and the other jobs in the order they came, until their bytes reach the budget. What is not
 * recorded stays for the next iteration.
 *
 * The copies are recorded on a ring of two command buffer pools of the transfer family, never on
 * the pool of a frame slot: a frame slot waits for the presentation of its last frame before it
 * can be used (FrameObjectManager::prepareNextFrame()), so an iteration that only uploads and used
 * a slot would make the frames wait for each other. The pool of an iteration is reset before it
 * records; that waits for the submit two iterations ago, which is expected to be done long before;
 * with large copies it may not be, and the reset then blocks the render thread until it is.
 */
class UploadQueue
{
public:
    /**
     * @param wake Told by push(), so that the render thread wakes for an upload without a frame.
     * @param backgroundBudget Bytes of jobs without a draw that one iteration records. A job that
     *        reaches it is recorded; a job bigger than it is recorded alone.
     */
    UploadQueue(const Graphics::Vulkan::DevicePtr&     device,
                Graphics::Vulkan::FencePoolPtr         fencePool,
                Graphics::Vulkan::TimelineSemaphorePtr timeline,
                FramePacketQueue&                      wake,
                std::uint64_t                          backgroundBudget);

    /** Any thread. Wakes the render thread. */
    void push(UploadJob job);

    /** Any thread: the jobs that no submit has taken yet, and the bytes of their staging. */
    [[nodiscard]] Renderer::UploadStatus status() const noexcept;

    /** Signaled by every copy: a copy that a ticket names has finished when the semaphore reached
     *  the value of the ticket. */
    [[nodiscard]] const Graphics::Vulkan::TimelineSemaphorePtr&
    timeline() const noexcept
    {
        return mTimeline;
    }

    /** Render thread. The value of the last submit of copies, which a submit that draws waits for.
     *  Rises only after the submit returned. */
    [[nodiscard]] std::uint64_t
    value() const noexcept
    {
        return mValue;
    }

    /**
     * Render thread. One iteration of the queue: takes what was pushed, drops the jobs whose
     * resource has no handle left (their copy is not recorded), records the jobs described above,
     * submits them to the transfer queue, and stamps the tickets of the jobs it submitted with the
     * value of the submit. Returns true if it recorded jobs and left others that another iteration
     * would record.
     */
    bool drain();

    /**
     * Render thread. Records into @p recorder, which draws, the acquire of every resource that
     * drain() copied since the last call. There is none when the transfer queue is the graphics
     * queue.
     */
    void acquireCopied(Graphics::Vulkan::CommandBufferRecorder& recorder);

private:
    /** Lets the tests read the queue. */
    friend struct RendererTestAccess;

    /** Takes the jobs out of mArrived into mHeld, dropping the dead ones. */
    void takeArrived();

    using Copied = std::variant<Graphics::Vulkan::BufferPtr, Graphics::Vulkan::TexturePtr>;

    std::shared_ptr<const Graphics::Vulkan::Queue> mTransferQueue;
    std::shared_ptr<const Graphics::Vulkan::Queue> mGraphicsQueue;
    Graphics::Vulkan::FencePoolPtr                 mFencePool;
    Graphics::Vulkan::TimelineSemaphorePtr         mTimeline;
    FramePacketQueue&                              mWake;
    const std::uint64_t                            mBackgroundBudget;

    std::array<Graphics::Vulkan::CommandBufferPoolPtr, 2u> mRing;
    std::size_t                                            mNextPool = 0u;

    /** Render thread only. */
    std::uint64_t mValue = 0u;
    /** Render thread only: what came from mArrived and was not recorded yet, in the order it was
     *  pushed. */
    std::vector<UploadJob> mHeld;
    /** Render thread only: copied resources the next frame has to acquire. */
    std::vector<Copied> mCopied;

    /** Pushed by any thread, taken by the render thread. */
    std::mutex             mMutex;
    std::vector<UploadJob> mArrived;

    std::atomic<std::size_t>   mPendingCount = 0u;
    std::atomic<std::uint64_t> mPendingBytes = 0u;
};
} // namespace VOG::Engine
