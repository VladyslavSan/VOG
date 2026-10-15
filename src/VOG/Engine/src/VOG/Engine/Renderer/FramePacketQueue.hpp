#pragma once

#include "VOG/Engine/Renderer/Camera.hpp"
#include "VOG/Engine/Renderer/Renderable.hpp"

#include <condition_variable>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace VOG::Engine
{
/**
 * One frame, as the thread that submits it saw it. It holds CPU-side objects only (meshes,
 * materials, a camera), none of the render-thread-only objects of ResourceOwnership.md, so a
 * packet may die on any thread.
 */
struct FramePacket
{
    /** Every draw has a mesh and a material. */
    RenderQueue draws;
    Camera      camera;
};

/**
 * Hands packets from the submitting thread to the render thread: one slot, so the submitter runs
 * at most one packet ahead. Both blocking calls wake for a stop request.
 */
class FramePacketQueue
{
public:
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

    bool
    empty() const
    {
        const std::scoped_lock lock{mMutex};

        return !mSlot.has_value();
    }

private:
    mutable std::mutex          mMutex;
    std::condition_variable_any mChanged;
    std::optional<FramePacket>  mSlot;
};
} // namespace VOG::Engine
