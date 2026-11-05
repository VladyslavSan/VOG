#include <VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <tuple>
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
    device->getGraphicsQueue()->submit(fencePool, {.commandBuffers = std::span{&executable, 1u}});
}

/** Records and submits a command buffer that clears @p target; heavy enough to still run later. */
ExecutableCommandBuffer
recordClear(const CommandBufferPoolPtr& pool, const RenderTargetPtr& target)
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

RenderTargetPtr
makeTarget(Device& device, std::uint32_t size)
{
    return createColorTarget(device, size);
}

/**
 * A format that the device can use as a depth attachment: the spec makes at least one of
 * D32_SFLOAT and X8_D24_UNORM_PACK32 available, so this is vk::Format::eUndefined only on a device
 * that breaks it.
 */
vk::Format
mandatoryDepthFormat(const Device& device)
{
    for (const vk::Format candidate : {vk::Format::eD32Sfloat, vk::Format::eX8D24UnormPack32})
    {
        const auto properties = device.raiiPhysicalDevice().getFormatProperties(candidate);
        if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        {
            return candidate;
        }
    }

    return vk::Format::eUndefined;
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
    std::shared_ptr<HeapBuffer>     block = mDevice->createHeapBuffer(heap, gBlockBytes.size());
    const std::weak_ptr<Texture>    weakTexture = texture;
    const std::weak_ptr<HeapBuffer> weakBlock   = block;

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
 * are equal and it records no barrier). copyImageToBuffer retains its image and its destination
 * too: the barrier that readies the image is in a command buffer of another pool, which is reset
 * first, so only the copy keeps them. Under the validation layer, so that the barriers and the
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
    RenderTargetPtr         target = createColorTarget(*mDevice, 2u);
    std::shared_ptr<Buffer> readback =
        mDevice->createBuffer({.size  = 2u * 2u * vk::blockSize(vk::Format::eR8G8B8A8Unorm),
                               .usage = vk::BufferUsageFlagBits::eTransferDst},
                              {});
    const std::array<std::weak_ptr<const void>, 8> weakResources{imageStaging,
                                                                 bufferStaging,
                                                                 texture,
                                                                 buffer,
                                                                 acquiredTexture,
                                                                 acquiredBuffer,
                                                                 target,
                                                                 readback};

    auto fencePool   = FencePool::create(mDevice);
    auto pool        = mDevice->createCommandBufferPool();
    auto barrierPool = mDevice->createCommandBufferPool();

    auto preparer = barrierPool->record();
    preparer.setImageBarrier(target,
                             {.srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
                              .dstStageMask        = vk::PipelineStageFlagBits2::eCopy,
                              .dstAccessMask       = vk::AccessFlagBits2::eTransferRead,
                              .oldLayout           = vk::ImageLayout::eUndefined,
                              .newLayout           = vk::ImageLayout::eTransferSrcOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                      .levelCount = 1u,
                                                      .layerCount = 1u}});
    auto preparing = preparer.finish();

    auto reader = pool->record();
    reader.copyImageToBuffer(target, readback);
    auto reading = reader.finish();

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
    target.reset();
    readback.reset();
    const auto expectAlive = [&](bool alive)
    {
        for (std::size_t i = 0u; i < weakResources.size(); ++i)
        {
            EXPECT_EQ(!weakResources[i].expired(), alive)
                << "resource " << i << " (in the order imageStaging, bufferStaging, texture, "
                << "buffer, acquiredTexture, acquiredBuffer, target, readback)";
        }
    };
    expectAlive(true);

    submitAlone(mDevice, *fencePool, copying);
    submitAlone(mDevice, *fencePool, acquiring);
    submitAlone(mDevice, *fencePool, preparing);
    submitAlone(mDevice, *fencePool, reading);
    expectAlive(true);

    // The barrier's command buffer lets go of the image; the copy still holds image and buffer.
    barrierPool->reset();
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
                  {.commandBuffers = std::span{&first, 1u},
                   .signals        = std::array<SemaphorePtr, 2>{chained, signalOnly},
                   .timelineSignals =
                       std::array{Queue::TimelineSignal{.semaphore = timeline, .value = 1u},
                                  Queue::TimelineSignal{.semaphore = timelineOnly, .value = 1u}}});

    auto second = recordEmpty(waiter);
    queue->submit(*fencePool,
                  {.waits          = std::array{Queue::SemaphoreWait{
                       .semaphore = chained, .stage = vk::PipelineStageFlagBits2::eAllCommands}},
                   .commandBuffers = std::span{&second, 1u},
                   .timelineWaits  = std::array{
                       Queue::TimelineWait{.semaphore = timeline,
                                           .value     = 1u,
                                           .stage = vk::PipelineStageFlagBits2::eAllCommands}}});

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

/**
 * counterValue() reads the value a submit signalled, from a thread other than the one that
 * submitted. It is 0 before any signal, and rises with each signal: the value is the one the
 * device holds, not one the semaphore remembers.
 */
TEST_F(VulkanFixture, TimelineSemaphore_counterValueIsReadOnAnotherThread)
{
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    auto timeline  = mDevice->createTimelineSemaphore();

    const auto readOnAnotherThread = [&timeline]
    {
        std::uint64_t value = 0u;
        std::thread{[&]
                    {
                        value = timeline->counterValue();
                    }}
            .join();

        return value;
    };
    EXPECT_EQ(readOnAnotherThread(), 0u);

    for (const std::uint64_t signalled : {3u, 7u})
    {
        auto executable = recordEmpty(pool);
        mDevice->getGraphicsQueue()->submit(*fencePool,
                                            {.commandBuffers  = std::span{&executable, 1u},
                                             .timelineSignals = std::array{Queue::TimelineSignal{
                                                 .semaphore = timeline, .value = signalled}}});

        // Waits the fence of the submit, which is signalled after the semaphores of the submit.
        pool->reset();
        EXPECT_EQ(readOnAnotherThread(), signalled);
        EXPECT_EQ(timeline->counterValue(), signalled);
    }
}

namespace
{
/**
 * Copies @p staging into a buffer on a pool of the transfer family, signalling the
 * timeline at 1, then on the graphics queue reads it back into a host buffer and returns the
 * bytes. The graphics submit waits for the timeline at 1 only if @p waitForUpload.
 *
 * The host buffer is read through mapForRead(); no production code needs a readback.
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
                             {.flags = AllocationFlag::eHostRandomAccess});

    auto fencePool    = FencePool::create(device);
    auto transferPool = device->createCommandBufferPool(transferFamily);
    auto graphicsPool = device->createCommandBufferPool(graphicsFamily);
    auto timeline     = device->createTimelineSemaphore();

    auto transferRecorder = transferPool->record();
    transferRecorder.copyBuffer(staging, deviceLocal, graphicsFamily);
    auto transferCommands = transferRecorder.finish();
    device->getTransferQueue()->submit(
        *fencePool,
        {.commandBuffers  = std::span{&transferCommands, 1u},
         .timelineSignals = std::array{Queue::TimelineSignal{.semaphore = timeline, .value = 1u}}});

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
    device->getGraphicsQueue()->submit(
        *fencePool,
        {.commandBuffers = std::span{&graphicsCommands, 1u},
         .timelineWaits =
             waitForUpload ? std::span{waits} : std::span<const Queue::TimelineWait>{}});

    // Resetting the pools waits for their fences.
    transferPool->reset();
    graphicsPool->reset();

    const auto mapping = readBack->mapForRead();
    const auto bytes   = mapping.bytes().first(size);
    return {bytes.begin(), bytes.end()};
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
    mDevice->getGraphicsQueue()->submit(*fencePool, {.commandBuffers = executables});

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
TEST_F(VulkanValidationFixture, CommandBufferRecorder_clearRenderTargetAndSubmit)
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

/**
 * A render target is cleared and read back: copyImageToBuffer puts the pixels the pass wrote into a
 * host buffer. Every channel has its own value, so a swapped channel, a copy that never ran or one
 * of the wrong size shows. Under the validation layer, which checks the layouts of the barriers
 * around the copy.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_clearedRenderTargetReadsBack)
{
    constexpr std::uint32_t kSize = 8u;

    auto target    = createColorTarget(*mDevice, kSize);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering(
            {{.attachment = target,
              .loadOp     = vk::AttachmentLoadOp::eClear,
              .storeOp    = vk::AttachmentStoreOp::eStore,
              .clearValue = vk::ClearColorValue{std::array{0.2f, 0.4f, 0.6f, 0.8f}}}});
    }
    const BufferPtr readback   = recordReadback(*mDevice, recorder, target);
    auto            executable = recorder.finish();
    submitAlone(mDevice, *fencePool, executable);
    pool->reset();

    // 0.2, 0.4, 0.6 and 0.8 of 255, which are whole numbers.
    constexpr std::array<std::byte, 4> kPixel{
        std::byte{51}, std::byte{102}, std::byte{153}, std::byte{204}};
    const auto mapping = readback->mapForRead();
    const auto bytes   = mapping.bytes().first(kSize * kSize * kPixel.size());
    for (std::size_t pixel = 0u; pixel < kSize * kSize; ++pixel)
    {
        EXPECT_TRUE(std::ranges::equal(bytes.subspan(pixel * kPixel.size(), kPixel.size()), kPixel))
            << "pixel " << pixel;
    }
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * A depth render target is rendered into: its view has the depth aspect, and a pass with only a
 * depth attachment clears it. The spec makes at least one of X8_D24_UNORM_PACK32 and D32_SFLOAT
 * available as a depth attachment, so the test takes the first the device offers. Under the
 * validation layer, which checks the aspect of the view and the layouts of the barrier and the
 * pass.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_depthRenderTargetIsRenderedInto)
{
    const vk::Format format = mandatoryDepthFormat(*mDevice);
    ASSERT_NE(format, vk::Format::eUndefined);

    RenderTargetPtr depth = mDevice->createRenderTarget(
        {.width = 16u, .height = 16u}, format, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();

    auto recorder = pool->record();
    recorder.setImageBarrier(depth,
                             {.srcStageMask  = vk::PipelineStageFlagBits2::eTopOfPipe,
                              .dstStageMask  = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                               vk::PipelineStageFlagBits2::eLateFragmentTests,
                              .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                              .oldLayout     = vk::ImageLayout::eUndefined,
                              .newLayout     = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = {.aspectMask = vk::ImageAspectFlagBits::eDepth,
                                                      .levelCount = 1u,
                                                      .layerCount = 1u}});
    {
        auto pass = recorder.beginRendering(
            {}, CommandBufferRecorder::DepthAttachment{.attachment = depth});
    }
    auto executable = recorder.finish();
    submitAlone(mDevice, *fencePool, executable);
    pool->reset();

    EXPECT_EQ(depth->getFormat(), format);
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * A render target needs an extent the device can make, an attachment usage, and a format without a
 * stencil aspect (nothing renders into one); each is refused before an image exists, as
 * std::invalid_argument.
 */
TEST_F(VulkanFixture, Device_createRenderTargetRefusesWhatIsNoAttachment)
{
    constexpr auto kFormat = vk::Format::eR8G8B8A8Unorm;
    constexpr auto kColor  = vk::ImageUsageFlagBits::eColorAttachment;

    EXPECT_NO_THROW(std::ignore =
                        mDevice->createRenderTarget({.width = 1u, .height = 1u}, kFormat, kColor));
    EXPECT_THROW(std::ignore =
                     mDevice->createRenderTarget({.width = 0u, .height = 4u}, kFormat, kColor),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     mDevice->createRenderTarget({.width = 4u, .height = 0u}, kFormat, kColor),
                 std::invalid_argument);
    const std::uint32_t tooWide = mDevice->limits.maxImageDimension2D + 1u;
    EXPECT_THROW(std::ignore =
                     mDevice->createRenderTarget({.width = tooWide, .height = 4u}, kFormat, kColor),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = mDevice->createRenderTarget(
                     {.width = 4u, .height = 4u}, kFormat, vk::ImageUsageFlagBits::eSampled),
                 std::invalid_argument);
    for (const vk::Format stencil :
         {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint, vk::Format::eS8Uint})
    {
        EXPECT_THROW(std::ignore = mDevice->createRenderTarget(
                         {.width = 4u, .height = 4u},
                         stencil,
                         vk::ImageUsageFlagBits::eDepthStencilAttachment),
                     std::invalid_argument)
            << vk::to_string(stencil);
    }
}

#ifndef NDEBUG
/**
 * The recorder asserts the usage rules that the driver would take as undefined behaviour: a
 * staging buffer that does not cover the image, and push data that is empty, off a multiple of 4
 * or past maxPushDataSize. The death runs in a fresh process, because the process that runs Vulkan
 * has threads.
 */
TEST_F(VulkanFixture, CommandBufferRecorderDeathTest_misusedUploadAndPushDataAssert)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    auto heap = createHeap();
    // A 4x4 RGBA8 image takes 64 bytes; the staging buffer has 16.
    std::shared_ptr<Buffer>  staging = createStaging(gBlockBytes);
    std::shared_ptr<Texture> texture =
        mDevice->createTexture(heap, {.width = 4u, .height = 4u}, vk::Format::eR8G8B8A8Srgb);
    auto pool = mDevice->createCommandBufferPool();

    const auto useStderr = []
    {
        // The default logger writes to stdout; a death test reads stderr.
        spdlog::set_default_logger(spdlog::stderr_logger_mt("assert"));
    };

    EXPECT_DEATH(
        {
            useStderr();
            auto recorder = pool->record();
            recorder.copyBufferToTexture(
                staging, texture, mDevice->queueInfos.graphics.familyIndex);
        },
        "smaller than the image");

    const auto pushData = [&](std::uint32_t offset, std::size_t size)
    {
        useStderr();
        auto                         recorder = pool->record();
        auto                         target   = makeTarget(*mDevice, 16u);
        const std::vector<std::byte> bytes(size);
        recorder.setImageBarrier(target, toColorAttachmentBarrier());
        auto pass = recorder.beginRendering({{.attachment = target}});
        pass.pushData(offset, bytes);
    };
    EXPECT_DEATH(pushData(0u, 0u), "at least one byte");
    EXPECT_DEATH(pushData(2u, 4u), "multiples of 4");
    EXPECT_DEATH(pushData(0u, 6u), "multiples of 4");
    EXPECT_DEATH(pushData(0u, mDevice->descriptorHeapProperties.maxPushDataSize + 4u),
                 "past maxPushDataSize");

    // copyImageToBuffer: inside a rendering pass, an image with a depth aspect, and a destination
    // smaller than the image.
    const BufferPtr       roomy = createStaging(std::vector<std::byte>(4096u));
    const RenderTargetPtr color = makeTarget(*mDevice, 16u);
    EXPECT_DEATH(
        {
            useStderr();
            auto recorder = pool->record();
            recorder.setImageBarrier(color, toColorAttachmentBarrier());
            auto pass = recorder.beginRendering({{.attachment = color}});
            recorder.copyImageToBuffer(color, roomy);
        },
        "outside a RenderingPass");

    const RenderTargetPtr depth = mDevice->createRenderTarget(
        {.width = 16u, .height = 16u},
        mandatoryDepthFormat(*mDevice),
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc);
    EXPECT_DEATH(
        {
            useStderr();
            auto recorder = pool->record();
            recorder.copyImageToBuffer(depth, roomy);
        },
        "color aspect only");

    EXPECT_DEATH(
        {
            useStderr();
            auto recorder = pool->record();
            recorder.copyImageToBuffer(color, staging);
        },
        "destination of copyImageToBuffer is smaller");
}

/** A buffer maps only the way its allocation flag allows: reading needs eHostRandomAccess. */
TEST_F(VulkanFixture, BufferDeathTest_mappingNeedsTheFlagOfItsDirection)
{
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    const auto makeBuffer = [&](AllocationFlag flag)
    {
        return mDevice->createBuffer({.size = 16u, .usage = vk::BufferUsageFlagBits::eTransferDst},
                                     {.flags = flag});
    };

    // Each direction works on the buffer that was made for it.
    EXPECT_EQ(makeBuffer(AllocationFlag::eHostSequentialWrite)->mapForWrite().bytes().size() >= 16u,
              true);
    EXPECT_EQ(makeBuffer(AllocationFlag::eHostRandomAccess)->mapForRead().bytes().size() >= 16u,
              true);

    const auto useStderr = []
    {
        spdlog::set_default_logger(spdlog::stderr_logger_mt("assert"));
    };
    EXPECT_DEATH(
        {
            useStderr();
            std::ignore = makeBuffer(AllocationFlag::eHostSequentialWrite)->mapForRead();
        },
        "eHostRandomAccess");
}
#endif
} // namespace VOG::Tests
