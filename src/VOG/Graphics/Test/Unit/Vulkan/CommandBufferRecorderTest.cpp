#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <span>
#include <type_traits>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
// The raw Vulkan bases are private: recording and submission must go through the recorder and the
// queue, which retain what they touch.
static_assert(!std::is_convertible_v<CommandBuffer*, vk::raii::CommandBuffer*>);
static_assert(!std::is_convertible_v<Queue*, vk::raii::Queue*>);
static_assert(!std::is_convertible_v<Fence*, vk::raii::Fence*>);

namespace
{
constexpr vk::DeviceSize gBufferSize = 256u;

/** Submits @p executable on the graphics queue with nothing to wait and nothing to signal. */
void
submitAlone(const DevicePtr& device, FencePool& fencePool, ExecutableCommandBuffer& executable)
{
    device->getGraphicsQueue()->submit(fencePool, {}, std::span{&executable, 1u}, {});
}

/** Records and submits a command buffer that clears @p target; heavy enough to still run later. */
ExecutableCommandBuffer
recordClear(const CommandBufferPoolPtr& pool, const std::shared_ptr<RenderBuffer>& target)
{
    auto recorder = pool->record();
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering(
            {{.attachment = target,
              .loadOp     = vk::AttachmentLoadOp::eClear,
              .storeOp    = vk::AttachmentStoreOp::eStore,
              .clearValue = vk::ClearColorValue{std::array{0.25f, 0.5f, 0.75f, 1.0f}}}});
    }
    return recorder.finish();
}

std::shared_ptr<RenderBuffer>
makeTarget(Device& device, std::uint32_t size)
{
    return device.createRenderBuffer(AttachmentUsage::eColor,
                                     vk::Format::eR8G8B8A8Unorm,
                                     {.width = size, .height = size},
                                     SampleCount::e1,
                                     1u,
                                     1u,
                                     vk::ImageTiling::eOptimal,
                                     vk::ImageLayout::eUndefined,
                                     vk::MemoryPropertyFlagBits::eDeviceLocal);
}

vk::BufferMemoryBarrier2
vertexBufferBarrier()
{
    return {.srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
            .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
            .dstStageMask        = vk::PipelineStageFlagBits2::eVertexInput,
            .dstAccessMask       = vk::AccessFlagBits2::eVertexAttributeRead,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .offset              = 0u,
            .size                = VK_WHOLE_SIZE};
}
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

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.setBufferBarrier(buffer, vertexBufferBarrier());
    auto executable = recorder.finish();

    buffer.reset();
    EXPECT_FALSE(weakBuffer.expired());

    submitAlone(mDevice, *fencePool, executable);
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

    auto first = recordEmpty(signaller);
    queue->submit(
        *fencePool, {}, std::span{&first, 1u}, std::array<SemaphorePtr, 2>{chained, signalOnly});

    auto second = recordEmpty(waiter);
    queue->submit(*fencePool,
                  std::array{Queue::SemaphoreWait{
                      .semaphore = chained, .stage = vk::PipelineStageFlagBits2::eAllCommands}},
                  std::span{&second, 1u},
                  {});

    chained.reset();
    signalOnly.reset();
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
    auto target    = makeTarget(*mDevice, 2048u);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    {
        auto executable = recordClear(pool, target);
        submitAlone(mDevice, *fencePool, executable);
    }

    // Neither the caller nor the test waits: the pool owns the pending submission.
    pool.reset();
    fencePool.reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * One submit can guard command buffers of several pools with one fence. Dropping a pool then does
 * not drop the last reference to the fence, so only the pool's own wait keeps its command buffers
 * from being freed while the GPU still runs them.
 */
TEST_F(VulkanValidationFixture, CommandBufferPool_dropWaitsForFenceSharedWithAnotherPool)
{
    auto target    = makeTarget(*mDevice, 2048u);
    auto fencePool = FencePool::create(mDevice);
    auto heavy     = mDevice->createCommandBufferPool();
    auto light     = mDevice->createCommandBufferPool();

    std::array executables{recordClear(heavy, target), recordEmpty(light)};
    mDevice->getGraphicsQueue()->submit(*fencePool, {}, executables, {});

    // `light` still holds the fence, so dropping `heavy` is not the last reference to it.
    heavy.reset();
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());

    light.reset();
    fencePool.reset();
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * Dropping a recorder discards the recording: what it retained is released at once, not at the
 * next reset(), and the discarded buffer is not handed out again before then. It is still in the
 * recording state, so reusing it would begin a buffer that is already begun.
 */
TEST_F(VulkanValidationFixture, CommandBufferPool_droppedRecorderReleasesAndIsNotReused)
{
    std::shared_ptr<Buffer> buffer = mDevice->createBuffer(
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.usage = VMA_MEMORY_USAGE_AUTO});
    const std::weak_ptr<Buffer> weakBuffer = buffer;

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    {
        auto recorder = pool->record();
        recorder.setBufferBarrier(buffer, vertexBufferBarrier());
        buffer.reset();
        EXPECT_FALSE(weakBuffer.expired());
    }
    EXPECT_TRUE(weakBuffer.expired());

    auto executable = recordEmpty(pool);
    submitAlone(mDevice, *fencePool, executable);
    pool->reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * Dropping a finished command buffer without submitting it is a legal discard too. The pool has no
 * reset-command-buffer flag, so beginning the executable buffer again would be
 * VUID-vkBeginCommandBuffer-00050: it must wait for the pool reset.
 */
TEST_F(VulkanValidationFixture, CommandBufferPool_droppedExecutableIsNotReused)
{
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    {
        auto discarded = recordEmpty(pool);
    }

    auto executable = recordEmpty(pool);
    submitAlone(mDevice, *fencePool, executable);
    pool->reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * The whole frame path without a swapchain: barrier, cleared rendering pass, submit, reset.
 * Runs under the validation layer: a driver accepts a null view or a missing vkCmdEndRendering
 * silently, so only the layer can fail this test. The barrier is passed without an image: the
 * recorder fills it from the attachment, and a null image is a validation error.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_clearRenderBufferAndSubmit)
{
    auto target    = makeTarget(*mDevice, 64u);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto executable = recordClear(pool, target);
    submitAlone(mDevice, *fencePool, executable);

    pool->reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}
} // namespace VOG::Tests
