#pragma once

#include "VOG/Engine/Renderer/Renderable.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

namespace VOG::Engine
{
/**
 * One frame, as the thread that submits it saw it. It holds CPU-side objects only (meshes,
 * materials, a camera view), none of the render-thread-only objects of ResourceOwnership.md, so a
 * packet may die on any thread.
 */
struct FramePacket
{
    using Draws = std::vector<RenderQueue::Draw>;

    /** Every draw has a mesh and a material, and has passed the check of RenderQueue::submit(). */
    Draws draws;

    /** The view of the frame: the last one that an onDraw() set. */
    CameraView camera;

    /** What onDraw() saw of this frame; the Frame block of the shaders holds its time. */
    FrameContext frame;
};

/**
 * Hands packets from the submitting thread to the render thread: one slot, so the submitter runs
 * at most one packet ahead. It also tells the render thread that uploads are waiting, so that one
 * wait covers everything the render thread waits for, and hands the empty vector of the draws of
 * a rendered packet back to the submitting thread. Every blocking call wakes for a stop request.
 */
class FramePacketQueue
{
public:
    /** What the render thread woke up for. Both can be set. */
    struct Work
    {
        std::optional<FramePacket> packet;
        /** Uploads were queued since the last wait. */
        bool uploads = false;
    };

    /**
     * Waits until the slot is empty, then fills it. If stop is requested while the slot is full,
     * returns and drops @p packet.
     */
    void
    push(FramePacket packet, const std::stop_token& stopToken)
    {
        std::unique_lock lock{mMutex};
        if (!mChanged.wait(lock,
                           stopToken,
                           [this]
                           {
                               return !mSlot.has_value();
                           }))
        {
            return;
        }

        mSlot.emplace(std::move(packet));
        mChanged.notify_all();
    }

    /** Never waits: fills the slot if it is empty, otherwise drops @p packet. */
    void
    tryPush(FramePacket packet)
    {
        const std::scoped_lock lock{mMutex};
        if (mSlot)
        {
            return;
        }

        mSlot.emplace(std::move(packet));
        mChanged.notify_all();
    }

    /**
     * Waits until the slot is full and takes the packet. If stop is requested while the slot is
     * empty, returns nothing. A queued packet is returned even after stop was requested.
     */
    std::optional<FramePacket>
    pop(const std::stop_token& stopToken)
    {
        std::unique_lock lock{mMutex};
        mChanged.wait(lock,
                      stopToken,
                      [this]
                      {
                          return mSlot.has_value();
                      });

        std::optional<FramePacket> packet = std::exchange(mSlot, std::nullopt);
        if (packet)
        {
            mChanged.notify_all();
        }

        return packet;
    }

    /**
     * Waits until the slot is full or uploads are marked, and takes both. If stop is requested
     * with neither, returns nothing. This is the one wait of the render thread.
     */
    Work
    wait(const std::stop_token& stopToken)
    {
        std::unique_lock lock{mMutex};
        mChanged.wait(lock,
                      stopToken,
                      [this]
                      {
                          return mSlot.has_value() || mUploadsPending;
                      });

        Work work{.packet  = std::exchange(mSlot, std::nullopt),
                  .uploads = std::exchange(mUploadsPending, false)};
        if (work.packet)
        {
            mChanged.notify_all();
        }

        return work;
    }

    /**
     * Any thread. Tells the render thread that uploads are waiting. The flag is set under the
     * mutex that the predicate of wait() is checked under, before the notify: a notify with the
     * flag set outside of it can land between the check of a waiter and its sleep, and the waiter
     * would sleep until the next packet, which never comes while the submitting thread is parked.
     */
    void
    markUploads()
    {
        {
            const std::scoped_lock lock{mMutex};
            mUploadsPending = true;
        }
        mChanged.notify_all();
    }

    /** Render thread. Keeps the capacity of the draws of a rendered packet for the next one. The
     *  draws are destroyed here, on the render thread, and before the lock. */
    void
    recycle(FramePacket::Draws draws)
    {
        draws.clear();

        const std::scoped_lock lock{mMutex};
        mRecycled = std::move(draws);
    }

    /** Submitting thread. The vector that recycle() kept, or an empty one. */
    FramePacket::Draws
    takeRecycled()
    {
        const std::scoped_lock lock{mMutex};

        return std::exchange(mRecycled, {});
    }

    bool
    empty() const
    {
        const std::scoped_lock lock{mMutex};

        return !mSlot.has_value();
    }

private:
    /** Lets the tests hold the mutex. */
    friend struct FramePacketQueueTestAccess;

    mutable std::mutex          mMutex;
    std::condition_variable_any mChanged;
    std::optional<FramePacket>  mSlot;
    bool                        mUploadsPending = false;
    FramePacket::Draws          mRecycled;
};
} // namespace VOG::Engine
