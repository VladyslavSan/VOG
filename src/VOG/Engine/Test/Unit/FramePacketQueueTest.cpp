#include "VOG/Engine/Renderer/FramePacketQueue.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <tuple>
#include <utility>

#include "HandleTestAccess.hpp"
#include "RenderQueueTestAccess.hpp"

using namespace VOG::Engine;

namespace VOG::Engine
{
/** Test-only window into the queue, befriended by FramePacketQueue. */
struct FramePacketQueueTestAccess
{
    /** Holds the mutex of the queue until the lock is released. */
    static std::unique_lock<std::mutex>
    lock(FramePacketQueue& queue)
    {
        return std::unique_lock{queue.mMutex};
    }
};
} // namespace VOG::Engine

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
    return FramePacket{.camera = CameraView{.nearPlane = marker}};
}

float
markerOf(const std::optional<FramePacket>& result)
{
    return result ? result->camera.nearPlane : -1.0f;
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

TEST(FramePacketQueueTest, tryPushWakesBlockedPop)
{
    FramePacketQueue           queue;
    std::stop_source           stop;
    std::optional<FramePacket> popped;

    Helper helper{[&]
                  {
                      popped = queue.pop(stop.get_token());
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    queue.tryPush(packet(gFirst));
    // A pop that tryPush did not wake never returns by itself: the deadline of returnsInTime()
    // fails the test, and the stop frees the helper.
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            stop.request_stop();
        }));
    EXPECT_EQ(markerOf(popped), gFirst);
}

TEST(FramePacketQueueTest, tryPushDropsWhileFull)
{
    FramePacketQueue queue;

    queue.tryPush(packet(gFirst));
    queue.tryPush(packet(gSecond));

    EXPECT_EQ(markerOf(popNow(queue)), gFirst);
    EXPECT_TRUE(queue.empty());
}

TEST(FramePacketQueueTest, waitWakesForUploadsWithoutAPacket)
{
    FramePacketQueue       queue;
    FramePacketQueue::Work work;

    Helper helper{[&]
                  {
                      work = queue.wait({});
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    queue.markUploads();
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            queue.push(packet(0.0f), {});
        }));
    EXPECT_FALSE(work.packet);
    EXPECT_TRUE(work.uploads);
}

/** One wait takes the packet and the mark, and clears the mark: the next wait does not see it
 *  again. */
TEST(FramePacketQueueTest, waitTakesThePacketAndTheMarkTogether)
{
    FramePacketQueue queue;
    std::stop_source stop;
    queue.markUploads();
    queue.push(packet(gFirst), {});
    stop.request_stop();

    const FramePacketQueue::Work first = queue.wait(stop.get_token());
    EXPECT_EQ(markerOf(first.packet), gFirst);
    EXPECT_TRUE(first.uploads);

    const FramePacketQueue::Work second = queue.wait(stop.get_token());
    EXPECT_FALSE(second.packet);
    EXPECT_FALSE(second.uploads);
}

TEST(FramePacketQueueTest, stopInterruptsBlockedWait)
{
    FramePacketQueue       queue;
    std::stop_source       stop;
    FramePacketQueue::Work work;

    Helper helper{[&]
                  {
                      work = queue.wait(stop.get_token());
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    stop.request_stop();
    EXPECT_TRUE(helper.returnsInTime(
        [&]
        {
            queue.markUploads();
        }));
    EXPECT_FALSE(work.packet);
    EXPECT_FALSE(work.uploads);
}

/**
 * The mark is set under the mutex that the predicate of wait() is checked under. A mark set outside
 * of it, with the notify after, can land between the check of a waiter and its sleep: the waiter
 * sleeps on, and the upload waits for a packet that never comes. A waiter cannot be held between
 * its check and its sleep, so the test holds the mutex instead: markUploads() must wait for it.
 */
TEST(FramePacketQueueTest, markUploadsWaitsForTheMutexOfThePredicate)
{
    FramePacketQueue queue;

    auto   lock = FramePacketQueueTestAccess::lock(queue);
    Helper helper{[&]
                  {
                      queue.markUploads();
                  }};
    EXPECT_TRUE(helper.stillBlocked());

    lock.unlock();
    EXPECT_TRUE(helper.returnsInTime(
        []
        {
        }));
}

/** The draws of a rendered packet come back empty, with their capacity, once. */
TEST(FramePacketQueueTest, recycleReturnsTheCapacityOfTheDrawsOnce)
{
    constexpr std::size_t kDraws = 8u;

    FramePacketQueue queue;
    EXPECT_EQ(queue.takeRecycled().capacity(), 0u);

    FramePacket rendered;
    for (std::size_t draw = 0u; draw < kDraws; ++draw)
    {
        RenderQueueTestAccess::addDraw(
            rendered, HandleTestAccess::makeMesh(), HandleTestAccess::makeMaterial(), {});
    }
    queue.recycle(std::move(rendered.draws));

    const FramePacket::Draws recycled = queue.takeRecycled();
    EXPECT_TRUE(recycled.empty());
    EXPECT_GE(recycled.capacity(), kDraws);
    EXPECT_EQ(queue.takeRecycled().capacity(), 0u);
}
} // namespace VOG::Tests
