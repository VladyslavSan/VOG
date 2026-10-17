#include "VOG/Engine/Renderer/FramePacketQueue.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <optional>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>

#include "RenderQueueTestAccess.hpp"

using namespace VOG::Engine;

namespace VOG::Tests
{
namespace
{
/** How long a call that could block may take. A test that waits longer has found a hang. */
constexpr std::chrono::seconds gDeadline{5};
/** How long a call that must block is watched before the test calls it blocked. */
constexpr std::chrono::milliseconds gStillBlocked{50};

/** Markers of the packets a test tells apart. */
constexpr float gFirst  = 1.0f;
constexpr float gSecond = 2.0f;

/** Packets differ by their camera, which nothing else in these tests reads. */
FramePacket
packet(float marker)
{
    FramePacket result;
    result.draws.setCamera(CameraView{.nearPlane = marker});

    return result;
}

float
markerOf(const std::optional<FramePacket>& result)
{
    return result ? RenderQueueTestAccess::camera(result->draws).nearPlane : -1.0f;
}

/** Takes the packet in the slot, or returns nothing if there is none: it never waits. */
std::optional<FramePacket>
popNow(FramePacketQueue& queue)
{
    std::stop_source stopped;
    stopped.request_stop();

    return queue.pop(stopped.get_token());
}

bool
isReady(std::future<void>& future, std::chrono::milliseconds timeout)
{
    return future.wait_for(timeout) == std::future_status::ready;
}

/**
 * Runs @p call on a helper thread. The tests check with isReady() whether it has returned, and
 * must make it return (@p unblock) before they join it, so that a broken queue fails a test
 * instead of hanging it.
 */
class Helper
{
public:
    explicit Helper(std::function<void()> call)
        : mFuture{std::async(std::launch::async, std::move(call))}
    {
    }

    bool
    stillBlocked()
    {
        return !isReady(mFuture, gStillBlocked);
    }

    /** Waits for the call to return, up to the deadline; @p unblock is the way out if it hangs. */
    template <class Unblock>
    bool
    returnsInTime(Unblock&& unblock)
    {
        const bool inTime = isReady(mFuture, gDeadline);
        if (!inTime)
        {
            ADD_FAILURE() << "A call did not return within " << gDeadline.count() << " s.";
            unblock();
        }
        mFuture.get();

        return inTime;
    }

private:
    std::future<void> mFuture;
};
} // namespace

TEST(FramePacketQueueTest, popWaitsForPush)
{
    FramePacketQueue           queue;
    std::optional<FramePacket> popped;

    Helper helper{[&]
                  {
                      popped = queue.pop({});
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    queue.push(packet(gFirst), {});
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            queue.push(packet(0.0f), {});
        }));
    EXPECT_EQ(markerOf(popped), gFirst);
}

TEST(FramePacketQueueTest, pushWaitsWhileFull)
{
    FramePacketQueue queue;
    queue.push(packet(gFirst), {});

    Helper helper{[&]
                  {
                      queue.push(packet(gSecond), {});
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    EXPECT_EQ(markerOf(popNow(queue)), gFirst);
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            std::ignore = popNow(queue);
        }));
    EXPECT_EQ(markerOf(popNow(queue)), gSecond);
}

TEST(FramePacketQueueTest, stopInterruptsBlockedPop)
{
    FramePacketQueue           queue;
    std::stop_source           stop;
    std::optional<FramePacket> popped = packet(0.0f);

    Helper helper{[&]
                  {
                      popped = queue.pop(stop.get_token());
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    stop.request_stop();
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            queue.push(packet(0.0f), {});
        }));
    EXPECT_FALSE(popped);
}

TEST(FramePacketQueueTest, stopInterruptsBlockedPush)
{
    FramePacketQueue queue;
    std::stop_source stop;
    queue.push(packet(gFirst), {});

    Helper helper{[&]
                  {
                      queue.push(packet(gSecond), stop.get_token());
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    stop.request_stop();
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            std::ignore = popNow(queue);
        }));

    // The packet that waited was dropped.
    EXPECT_EQ(markerOf(popNow(queue)), gFirst);
    EXPECT_TRUE(queue.empty());
}

TEST(FramePacketQueueTest, popAfterStopReturnsQueuedPacket)
{
    FramePacketQueue queue;
    std::stop_source stop;
    queue.push(packet(gFirst), {});
    stop.request_stop();

    EXPECT_EQ(markerOf(queue.pop(stop.get_token())), gFirst);
    EXPECT_FALSE(queue.pop(stop.get_token()));
}

TEST(FramePacketQueueTest, emptyReflectsSlot)
{
    FramePacketQueue queue;
    EXPECT_TRUE(queue.empty());

    queue.push(packet(gFirst), {});
    EXPECT_FALSE(queue.empty());

    std::ignore = popNow(queue);
    EXPECT_TRUE(queue.empty());
}

TEST(FramePacketQueueTest, tryPushDropsWhileFull)
{
    FramePacketQueue queue;

    queue.tryPush(packet(gFirst));
    queue.tryPush(packet(gSecond));

    EXPECT_EQ(markerOf(popNow(queue)), gFirst);
    EXPECT_TRUE(queue.empty());
}
} // namespace VOG::Tests
