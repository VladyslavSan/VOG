#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <type_traits>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
// The raw vk::raii::CommandBuffer base is private: recording must go through the recorder.
static_assert(!std::is_convertible_v<CommandBuffer*, vk::raii::CommandBuffer*>);

namespace
{
constexpr vk::DeviceSize gBufferSize = 256u;

constexpr std::uint64_t gWaitForever = std::numeric_limits<std::uint64_t>::max();

const std::array<SemaphorePtr, 0>           gNoSemaphores{};
const std::array<vk::PipelineStageFlags, 0> gNoStages{};
} // namespace

/**
 * A resource passed to a barrier is retained by the command buffer: it outlives the caller's last
 * reference and dies only when the pool resets, after the fence has signaled.
 */
TEST_F(VulkanFixture, CommandBufferRecorder_barrierRetainsBufferUntilPoolReset)
{
    std::shared_ptr<Buffer> buffer = mDevice->createBuffer(
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.usage = VMA_MEMORY_USAGE_AUTO});
    const std::weak_ptr<Buffer> weakBuffer = buffer;

    auto fencePool     = FencePool::create(mDevice);
    auto pool          = mDevice->createCommandBufferPool();
    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);

    {
        CommandBufferRecorder recorder{**commandBuffer};
        recorder.setBufferBarrier(buffer,
                                  {.srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
                                   .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                                   .dstStageMask        = vk::PipelineStageFlagBits2::eVertexInput,
                                   .dstAccessMask       = vk::AccessFlagBits2::eVertexAttributeRead,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .offset              = 0u,
                                   .size                = VK_WHOLE_SIZE});
        recorder.finish();
    }

    buffer.reset();
    EXPECT_FALSE(weakBuffer.expired());

    const auto submission = mDevice->getGraphicsQueue()->submit(
        fencePool, gNoSemaphores, gNoStages, std::array{std::move(commandBuffer)}, gNoSemaphores);
    ASSERT_EQ(submission.fence->wait(gWaitForever), vk::Result::eSuccess);
    EXPECT_FALSE(weakBuffer.expired());

    pool->reset();
    EXPECT_TRUE(weakBuffer.expired());
}

/**
 * Queue::submit makes the command buffers retain the semaphores they wait and signal, so the
 * caller can drop them. A semaphore dies when the last command buffer that used it is reset: the
 * wait semaphore outlives the signalling command buffer, and the signal-only one dies with it.
 */
TEST_F(VulkanFixture, Queue_submitRetainsSemaphoresUntilPoolReset)
{
    auto fencePool = FencePool::create(mDevice);
    auto queue     = mDevice->getGraphicsQueue();
    auto signaller = mDevice->createCommandBufferPool();
    auto waiter    = mDevice->createCommandBufferPool();

    SemaphorePtr                   chained        = mDevice->createSemaphore();
    SemaphorePtr                   signalOnly     = mDevice->createSemaphore();
    const std::weak_ptr<Semaphore> weakChained    = chained;
    const std::weak_ptr<Semaphore> weakSignalOnly = signalOnly;

    const auto submitEmpty = [&](const CommandBufferPoolPtr&                 pool,
                                 const std::vector<SemaphorePtr>&            waits,
                                 const std::array<vk::PipelineStageFlags, 1> stages,
                                 const std::vector<SemaphorePtr>&            signals)
    {
        auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
        {
            CommandBufferRecorder recorder{**commandBuffer};
            recorder.finish();
        }
        return queue->submit(
            fencePool, waits, stages, std::array{std::move(commandBuffer)}, signals);
    };

    const auto first  = submitEmpty(signaller, {}, {}, {chained, signalOnly});
    const auto second = submitEmpty(waiter, {chained}, {vk::PipelineStageFlagBits::eTopOfPipe}, {});

    chained.reset();
    signalOnly.reset();
    EXPECT_FALSE(weakChained.expired());
    EXPECT_FALSE(weakSignalOnly.expired());

    ASSERT_EQ(first.fence->wait(gWaitForever), vk::Result::eSuccess);
    ASSERT_EQ(second.fence->wait(gWaitForever), vk::Result::eSuccess);
    EXPECT_FALSE(weakChained.expired());
    EXPECT_FALSE(weakSignalOnly.expired());

    signaller->reset();
    EXPECT_FALSE(weakChained.expired());
    EXPECT_TRUE(weakSignalOnly.expired());

    waiter->reset();
    EXPECT_TRUE(weakChained.expired());
}

/**
 * Dropping a command buffer pool that still holds a submission waits for the submit fence first:
 * the command buffer must not be freed, nor its fence reset, while the GPU runs it. Only the
 * validation layer can tell, because a driver accepts both silently.
 */
TEST_F(VulkanValidationFixture, CommandBufferPool_dropWithPendingSubmissionWaitsForFence)
{
    std::shared_ptr<RenderBuffer> target =
        mDevice->createRenderBuffer(AttachmentUsage::eColor,
                                    vk::Format::eR8G8B8A8Unorm,
                                    {.width = 2048u, .height = 2048u},
                                    SampleCount::e1,
                                    1u,
                                    1u,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageLayout::eUndefined,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    {
        auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
        {
            CommandBufferRecorder recorder{**commandBuffer};
            recorder.setImageBarrier(target, toColorAttachmentBarrier(target->getImage()));
            {
                auto pass = recorder.beginRendering(
                    {{.attachment = target,
                      .loadOp     = vk::AttachmentLoadOp::eClear,
                      .storeOp    = vk::AttachmentStoreOp::eStore,
                      .clearValue = vk::ClearColorValue{std::array{0.25f, 0.5f, 0.75f, 1.0f}}}});
            }
            recorder.finish();
        }

        mDevice->getGraphicsQueue()->submit(fencePool,
                                            gNoSemaphores,
                                            gNoStages,
                                            std::array{std::move(commandBuffer)},
                                            gNoSemaphores);
    }

    // Neither the caller nor the test waits: the pool owns the pending submission.
    pool.reset();
    fencePool.reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * The whole frame path without a swapchain: barrier, cleared rendering pass, submit, fence wait.
 * Runs under the validation layer: a driver accepts a null view or a missing vkCmdEndRendering
 * silently, so only the layer can fail this test.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_clearRenderBufferAndSubmit)
{
    std::shared_ptr<RenderBuffer> target =
        mDevice->createRenderBuffer(AttachmentUsage::eColor,
                                    vk::Format::eR8G8B8A8Unorm,
                                    {.width = 64u, .height = 64u},
                                    SampleCount::e1,
                                    1u,
                                    1u,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageLayout::eUndefined,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto fencePool     = FencePool::create(mDevice);
    auto pool          = mDevice->createCommandBufferPool();
    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);

    {
        CommandBufferRecorder recorder{**commandBuffer};
        recorder.setImageBarrier(target, toColorAttachmentBarrier(target->getImage()));

        {
            auto pass = recorder.beginRendering(
                {{.attachment = target,
                  .loadOp     = vk::AttachmentLoadOp::eClear,
                  .storeOp    = vk::AttachmentStoreOp::eStore,
                  .clearValue = vk::ClearColorValue{std::array{0.25f, 0.5f, 0.75f, 1.0f}}}});
        }

        recorder.finish();
    }

    const auto submission = mDevice->getGraphicsQueue()->submit(
        fencePool, gNoSemaphores, gNoStages, std::array{std::move(commandBuffer)}, gNoSemaphores);

    EXPECT_EQ(submission.fence->wait(gWaitForever), vk::Result::eSuccess);

    pool->reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}
} // namespace VOG::Tests
