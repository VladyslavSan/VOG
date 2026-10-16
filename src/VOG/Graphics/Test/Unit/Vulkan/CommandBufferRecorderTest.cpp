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
#include <vector>

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

/** 16 bytes: a 2x2 RGBA8 texture, or a minimal uniform block. */
constexpr std::array<std::byte, 16> gBlockBytes{};

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
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer}, {});
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
 * An index buffer bound in a rendering pass is retained like a barrier's buffer. Under the
 * validation layer, so that the bind itself (buffer usage, offset alignment) is checked too.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_bindIndexBufferRetainsBufferUntilPoolReset)
{
    std::shared_ptr<Buffer> buffer = mDevice->createBuffer(
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eIndexBuffer}, {});
    const std::weak_ptr<Buffer> weakBuffer = buffer;

    auto target    = makeTarget(*mDevice, 64u);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering({{.attachment = target,
                                              .loadOp     = vk::AttachmentLoadOp::eClear,
                                              .storeOp    = vk::AttachmentStoreOp::eStore}});
        pass.bindIndexBuffer(buffer, 0u, vk::IndexType::eUint32);
    }
    auto executable = recorder.finish();

    buffer.reset();
    EXPECT_FALSE(weakBuffer.expired());

    submitAlone(mDevice, *fencePool, executable);
    EXPECT_FALSE(weakBuffer.expired());

    pool->reset();
    EXPECT_TRUE(weakBuffer.expired());
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * A texture and a uniform block that a pass uses by heap index are retained like a bound buffer:
 * they outlive the caller's last reference and die only when the pool resets.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_useResourceRetainsUntilPoolReset)
{
    auto                     heap = createHeap();
    std::shared_ptr<Texture> texture =
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb);
    std::shared_ptr<UniformBlock>     block       = mDevice->createUniformBlock(heap, gBlockBytes);
    const std::weak_ptr<Texture>      weakTexture = texture;
    const std::weak_ptr<UniformBlock> weakBlock   = block;

    auto target    = makeTarget(*mDevice, 64u);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering({{.attachment = target}});
        pass.useResource(texture);
        pass.useResource(block);
    }
    auto executable = recorder.finish();

    texture.reset();
    block.reset();
    EXPECT_FALSE(weakTexture.expired());
    EXPECT_FALSE(weakBlock.expired());

    submitAlone(mDevice, *fencePool, executable);
    EXPECT_FALSE(weakTexture.expired());
    EXPECT_FALSE(weakBlock.expired());

    pool->reset();
    EXPECT_TRUE(weakTexture.expired());
    EXPECT_TRUE(weakBlock.expired());
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/** The heap bound into a command buffer outlives the caller's last reference, like a buffer. */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_bindDescriptorHeapRetainsHeap)
{
    auto                                heap     = createHeap();
    const std::weak_ptr<DescriptorHeap> weakHeap = heap;

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.bindDescriptorHeap(heap);
    auto executable = recorder.finish();

    heap.reset();
    EXPECT_FALSE(weakHeap.expired());

    submitAlone(mDevice, *fencePool, executable);
    EXPECT_FALSE(weakHeap.expired());

    pool->reset();
    EXPECT_TRUE(weakHeap.expired());
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * The upload commands retain what they reference: the staging buffers and the destinations of the
 * copies, and the resources that acquire() takes over (which it retains even where the families
 * are equal and it records no barrier). Under the validation layer, so that the barriers and the
 * copies are checked too.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_uploadCommandsRetainTheirResources)
{
    const std::uint32_t family = mDevice->queueInfos.graphics.familyIndex;

    auto                     heap          = createHeap();
    std::shared_ptr<Buffer>  imageStaging  = createStaging(gBlockBytes);
    std::shared_ptr<Buffer>  bufferStaging = createStaging(gBlockBytes);
    std::shared_ptr<Texture> texture =
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb);
    std::shared_ptr<Buffer> buffer = mDevice->createBuffer(
        {.size = gBlockBytes.size(), .usage = vk::BufferUsageFlagBits::eTransferDst}, {});
    std::shared_ptr<Texture> acquiredTexture =
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb);
    std::shared_ptr<Buffer> acquiredBuffer = mDevice->createBuffer(
        {.size = gBlockBytes.size(), .usage = vk::BufferUsageFlagBits::eVertexBuffer}, {});
    const std::array<std::weak_ptr<const void>, 6> weakResources{
        imageStaging, bufferStaging, texture, buffer, acquiredTexture, acquiredBuffer};

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto copier = pool->record();
    copier.copyBufferToTexture(imageStaging, texture, family);
    copier.copyBuffer(bufferStaging, buffer, family);
    auto copying = copier.finish();

    auto acquirer = pool->record();
    acquirer.acquire(acquiredTexture, family);
    acquirer.acquire(acquiredBuffer, family);
    auto acquiring = acquirer.finish();

    imageStaging.reset();
    bufferStaging.reset();
    texture.reset();
    buffer.reset();
    acquiredTexture.reset();
    acquiredBuffer.reset();
    const auto expectAlive = [&](bool alive)
    {
        for (std::size_t i = 0u; i < weakResources.size(); ++i)
        {
            EXPECT_EQ(!weakResources[i].expired(), alive)
                << "resource " << i << " (in the order imageStaging, bufferStaging, texture, "
                << "buffer, acquiredTexture, acquiredBuffer)";
        }
    };
    expectAlive(true);

    submitAlone(mDevice, *fencePool, copying);
    submitAlone(mDevice, *fencePool, acquiring);
    expectAlive(true);

    pool->reset();
    expectAlive(false);
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * Queue::submit makes the command buffers retain the semaphores they wait and signal, binary and
 * timeline, so the caller can drop them. A semaphore dies when the last command buffer that used it
 * is reset: the wait semaphore outlives the signalling command buffer, and the signal-only one dies
 * with it.
 */
TEST_F(VulkanFixture, Queue_submitRetainsSemaphoresUntilPoolReset)
{
    auto fencePool = FencePool::create(mDevice);
    auto queue     = mDevice->getGraphicsQueue();
    auto signaller = mDevice->createCommandBufferPool();
    auto waiter    = mDevice->createCommandBufferPool();

    SemaphorePtr                           chained          = mDevice->createSemaphore();
    SemaphorePtr                           signalOnly       = mDevice->createSemaphore();
    TimelineSemaphorePtr                   timeline         = mDevice->createTimelineSemaphore();
    TimelineSemaphorePtr                   timelineOnly     = mDevice->createTimelineSemaphore();
    const std::weak_ptr<Semaphore>         weakChained      = chained;
    const std::weak_ptr<Semaphore>         weakSignalOnly   = signalOnly;
    const std::weak_ptr<TimelineSemaphore> weakTimeline     = timeline;
    const std::weak_ptr<TimelineSemaphore> weakTimelineOnly = timelineOnly;

    auto first = recordEmpty(signaller);
    queue->submit(*fencePool,
                  {},
                  std::span{&first, 1u},
                  std::array<SemaphorePtr, 2>{chained, signalOnly},
                  {},
                  std::array{Queue::TimelineSignal{.semaphore = timeline, .value = 1u},
                             Queue::TimelineSignal{.semaphore = timelineOnly, .value = 1u}});

    auto second = recordEmpty(waiter);
    queue->submit(
        *fencePool,
        std::array{Queue::SemaphoreWait{.semaphore = chained,
                                        .stage     = vk::PipelineStageFlagBits2::eAllCommands}},
        std::span{&second, 1u},
        {},
        std::array{Queue::TimelineWait{.semaphore = timeline,
                                       .value     = 1u,
                                       .stage     = vk::PipelineStageFlagBits2::eAllCommands}});

    chained.reset();
    signalOnly.reset();
    timeline.reset();
    timelineOnly.reset();
    EXPECT_FALSE(weakChained.expired());
    EXPECT_FALSE(weakSignalOnly.expired());
    EXPECT_FALSE(weakTimeline.expired());
    EXPECT_FALSE(weakTimelineOnly.expired());

    signaller->reset();
    EXPECT_FALSE(weakChained.expired());
    EXPECT_TRUE(weakSignalOnly.expired());
    EXPECT_FALSE(weakTimeline.expired());
    EXPECT_TRUE(weakTimelineOnly.expired());

    waiter->reset();
    EXPECT_TRUE(weakChained.expired());
    EXPECT_TRUE(weakTimeline.expired());
}

namespace
{
/**
 * Copies @p staging into a device-local buffer on a pool of the transfer family, signalling the
 * timeline at 1, then on the graphics queue reads it back into a host buffer and returns the
 * bytes. The graphics submit waits for the timeline at 1 only if @p waitForUpload.
 *
 * The host buffer is read through mapForWrite(), which is meant for writing: that is legal on
 * lavapipe, whose host memory is coherent, and no production code needs a readback.
 */
std::vector<std::byte>
uploadAndReadBack(const DevicePtr& device, const BufferPtr& staging, bool waitForUpload)
{
    const std::uint32_t  graphicsFamily = device->queueInfos.graphics.familyIndex;
    const std::uint32_t  transferFamily = device->queueInfos.transfer.familyIndex;
    const vk::DeviceSize size           = staging->byteSize();

    BufferPtr deviceLocal = device->createBuffer(
        {.size  = size,
         .usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc},
        {});
    BufferPtr readBack =
        device->createBuffer({.size = size, .usage = vk::BufferUsageFlagBits::eTransferDst},
                             {.flags = AllocationFlag::eHostSequentialWrite});

    auto fencePool    = FencePool::create(device);
    auto transferPool = device->createCommandBufferPool(transferFamily);
    auto graphicsPool = device->createCommandBufferPool(graphicsFamily);
    auto timeline     = device->createTimelineSemaphore();

    auto transferRecorder = transferPool->record();
    transferRecorder.copyBuffer(staging, deviceLocal, graphicsFamily);
    auto transferCommands = transferRecorder.finish();
    device->getTransferQueue()->submit(
        *fencePool,
        {},
        std::span{&transferCommands, 1u},
        {},
        {},
        std::array{Queue::TimelineSignal{.semaphore = timeline, .value = 1u}});

    auto graphicsRecorder = graphicsPool->record();
    graphicsRecorder.acquire(deviceLocal, transferFamily);
    graphicsRecorder.copyBuffer(deviceLocal, readBack, graphicsFamily);
    graphicsRecorder.setBufferBarrier(readBack,
                                      {.srcStageMask        = vk::PipelineStageFlagBits2::eCopy,
                                       .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                                       .dstStageMask        = vk::PipelineStageFlagBits2::eHost,
                                       .dstAccessMask       = vk::AccessFlagBits2::eHostRead,
                                       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                       .offset              = 0u,
                                       .size                = VK_WHOLE_SIZE});
    auto             graphicsCommands = graphicsRecorder.finish();
    const std::array waits{Queue::TimelineWait{
        .semaphore = timeline, .value = 1u, .stage = vk::PipelineStageFlagBits2::eAllCommands}};
    device->getGraphicsQueue()->submit(*fencePool,
                                       {},
                                       std::span{&graphicsCommands, 1u},
                                       {},
                                       waitForUpload ? std::span{waits}
                                                     : std::span<const Queue::TimelineWait>{});

    // Resetting the pools waits for their fences.
    transferPool->reset();
    graphicsPool->reset();

    const auto mapping = readBack->mapForWrite();
    return {mapping.data, mapping.data + size};
}

std::vector<std::byte>
sequenceBytes(std::size_t count)
{
    std::vector<std::byte> bytes(count);
    for (std::size_t i = 0u; i < count; ++i)
    {
        bytes[i] = static_cast<std::byte>(i + 1u);
    }
    return bytes;
}
} // namespace

/**
 * An upload on the transfer queue, read back on the graphics queue behind the timeline wait, is
 * ordered and complete: the synchronization validation reports no hazard and the bytes arrive.
 *
 * On lavapipe the transfer and the graphics queue are one VkQueue of one family, so this checks the
 * value wiring of the timeline (a signal or wait with the value dropped is a validation error or a
 * hazard) and that a pool of the transfer family submits on the transfer queue. It cannot check
 * the release and acquire barriers of two families, or a pool of the wrong family: there is only
 * one. The negative control below shows that the hazard check can see a missing wait.
 */
TEST_F(VulkanSyncValidationFixture, Queue_timelineWaitOrdersTransferCopyBeforeGraphicsRead)
{
    const auto bytes   = sequenceBytes(gBufferSize);
    const auto staging = createStaging(bytes);

    EXPECT_EQ(uploadAndReadBack(mDevice, staging, true), bytes);
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * Negative control of the test above: the same work without the wait must be reported as a
 * READ_AFTER_WRITE hazard. If the layer stayed silent, the silence above would mean nothing.
 */
TEST_F(VulkanSyncValidationFixture, Queue_missingTimelineWaitIsReportedBySyncval)
{
    const auto staging = createStaging(sequenceBytes(gBufferSize));

    (void)uploadAndReadBack(mDevice, staging, false);
    ASSERT_FALSE(mValidationErrors.empty()) << "synchronization validation reported nothing";
    EXPECT_NE(mValidationErrors.front().find("READ_AFTER_WRITE hazard"), std::string::npos)
        << mValidationErrors.front();
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
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer}, {});
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
