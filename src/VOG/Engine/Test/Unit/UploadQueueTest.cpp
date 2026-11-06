#include "VOG/Engine/Renderer/UploadQueue.hpp"

#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/UploadTicket.hpp"
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace VOG::Engine;

namespace VOG::Tests
{
namespace
{
constexpr std::uint64_t gJobBytes = 64u;

/**
 * An UploadQueue without a render thread: the test is the render thread, and calls drain() itself.
 * The budget is 0, so only a job whose ticket is referenced is recorded.
 */
class UploadQueueTest : public testing::Test
{
protected:
    /** A job of gJobBytes with a ticket; the buffer is the handle that keeps the job alive. */
    UploadJob
    makeJob(Graphics::Vulkan::BufferPtr& destination)
    {
        destination      = mDevice->createBuffer({.size  = gJobBytes,
                                                  .usage = vk::BufferUsageFlagBits::eVertexBuffer |
                                                           vk::BufferUsageFlagBits::eTransferDst},
                                                 {});
        auto ticket      = std::make_shared<UploadTicket>();
        ticket->timeline = mQueue.timeline();

        return {.staging = mDevice->createBuffer(
                    {.size = gJobBytes, .usage = vk::BufferUsageFlagBits::eTransferSrc}, {}),
                .destination = destination,
                .ticket      = std::move(ticket)};
    }

    Graphics::Vulkan::DevicePtr mDevice =
        Graphics::Vulkan::Instance::create(
            {.appName = "VOG UploadQueue Test", .engineName = "Test"})
            ->makeDevice();
    FramePacketQueue mPackets;
    UploadQueue      mQueue{mDevice,
                            Graphics::Vulkan::FencePool::create(mDevice),
                            mDevice->createTimelineSemaphore(),
                            mPackets,
                            0u};
};
} // namespace

/**
 * With a budget of 0 a job that no draw wants is not recorded, and drain() says so: it returns
 * false, because another iteration would find the same, and the render thread must not run one
 * without a wake (it would spin). Once a draw wants the job, the next drain records it, stamps
 * its ticket and leaves the other job, which a further drain would not record either.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(UploadQueueTest, budgetOfZeroRecordsOnlyReferencedJobsAndDoesNotAskForAnotherIteration)
{
    Graphics::Vulkan::BufferPtr wanted;
    Graphics::Vulkan::BufferPtr unwanted;
    UploadJob                   wantedJob = makeJob(wanted);
    const auto                  ticket    = wantedJob.ticket;

    mQueue.push(std::move(wantedJob));
    EXPECT_FALSE(mQueue.drain());
    EXPECT_EQ(ticket->value.load(), 0u);
    EXPECT_EQ(mQueue.status().pendingCount, 1u);
    EXPECT_EQ(mQueue.value(), 0u);

    mQueue.push(makeJob(unwanted));
    ticket->referenced = true;
    // It recorded a job and left another one behind.
    EXPECT_TRUE(mQueue.drain());
    EXPECT_NE(ticket->value.load(), 0u);
    EXPECT_EQ(ticket->value.load(), mQueue.value());
    EXPECT_EQ(mQueue.status().pendingCount, 1u);

    // What is left is not due: no iteration records it, and none is asked for.
    EXPECT_FALSE(mQueue.drain());
    EXPECT_EQ(mQueue.status().pendingCount, 1u);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!ticket->isResident() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    EXPECT_TRUE(ticket->isResident());
}
} // namespace VOG::Tests
