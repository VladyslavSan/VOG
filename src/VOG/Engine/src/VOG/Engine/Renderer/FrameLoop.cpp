#include "VOG/Engine/Renderer/FrameLoop.hpp"

#include <VOG/Common/Assert.hpp>
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/GpuContext.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include "VOG/Engine/Renderer/UploadQueue.hpp"
#include "VOG/Engine/Renderer/UploadTicket.hpp"
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>

#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <utility>

namespace VOG::Engine
{
static_assert(std::atomic<Extent>::is_always_lock_free);

namespace
{
constexpr std::chrono::milliseconds gZeroExtentBackoff{16};
/**
 * Upper bound on one wait for a free image. Stop latency is this plus one frame, plus the fence
 * waits of FrameObjectManager::prepareNextFrame, which have no timeout: they assume that the GPU
 * and the presentation engine make progress.
 */
constexpr std::chrono::milliseconds gAcquireTimeout{100};
constexpr std::size_t               gThreadId   = 0u;
constexpr std::array                gClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

FrameLoop::FrameLoop(GpuContext&       gpu,
                     UploadQueue&      uploads,
                     FramePacketQueue& packets,
                     Extent            extent)
    : mGpu{gpu}
    , mUploads{uploads}
    , mPackets{packets}
    , mRequestedExtent{extent}
    , mAppliedExtent{extent}
    , mPipelines{gpu.device}
    , mThread{[this](const std::stop_token& stopToken)
              {
                  run(stopToken);
              }}
{
}

FrameLoop::~FrameLoop() { stop(); }

void
FrameLoop::stop()
{
    if (mThread.joinable())
    {
        mThread.request_stop();
        mThread.join();
    }
}

void
FrameLoop::resize(Extent extent) noexcept
{
    mRequestedExtent.store(extent);
}

Renderer::State
FrameLoop::state() const noexcept
{
    return mState.load(std::memory_order_acquire);
}

std::exception_ptr
FrameLoop::failure() noexcept
{
    if (state() != Renderer::State::eFailed)
    {
        return nullptr;
    }

    mFailureCollected.store(true, std::memory_order_relaxed);

    return mFailure;
}

bool
FrameLoop::failureCollected() const noexcept
{
    return mFailureCollected.load(std::memory_order_relaxed);
}

std::stop_token
FrameLoop::stopToken() const noexcept
{
    return mThread.get_stop_token();
}

void
FrameLoop::run(const std::stop_token& stopToken)
{
    try
    {
        while (!stopToken.stop_requested())
        {
            FramePacketQueue::Work work = mPackets.wait(stopToken);
            if (!work.packet && !work.uploads)
            {
                continue;
            }

            if (work.packet)
            {
                markReferenced(*work.packet);
            }
            // Before the frame, with or without one: the uploads of a hidden or minimized window
            // are as due as those of a frame that is drawn. A frame that draws a resource finds
            // its copy recorded already, whatever the budget.
            if (mUploads.drain())
            {
                // What the budget left: another iteration, without a wake from anybody.
                mPackets.markUploads();
            }

            if (work.packet)
            {
                renderFrame(*work.packet);
                mPackets.recycle(std::move(work.packet->draws));
            }
        }
    }
    catch (...)
    {
        // Report first, so that state() shows the failure at once. Then keep consuming until
        // stop: submitFrame() must not wait for a consumer that is gone.
        mFailure = std::current_exception();
        mState.store(Renderer::State::eFailed, std::memory_order_release);
        while (mPackets.pop(stopToken))
        {
        }
    }
}

void
FrameLoop::markReferenced(const FramePacket& packet)
{
    const auto mark = [](UploadTicket& ticket)
    {
        // One relaxed load: a resource that was copied needs nothing.
        if (ticket.value.load(std::memory_order_relaxed) == 0u)
        {
            ticket.referenced = true;
        }
    };

    for (const auto& draw : packet.draws)
    {
        mark(*draw.mesh.mData->ticket);
        for (const Texture& texture : draw.material.mData->textures)
        {
            mark(*texture.mData->ticket);
        }
    }
}

bool
FrameLoop::applyExtent(Extent requested)
{
    if (!mGpu.swapchain->recreate(toVk(requested)))
    {
        return false;
    }

    mAppliedExtent = requested;

    return true;
}

void
FrameLoop::renderFrame(const FramePacket& packet)
{
    const Extent requested = mRequestedExtent.load();
    if (requested != mAppliedExtent && !applyExtent(requested))
    {
        // Zero-sized (minimized); back off instead of spinning.
        std::this_thread::sleep_for(gZeroExtentBackoff);
        return;
    }

    // Before acquiring: a pipeline that fails to build throws while no image is acquired.
    DrawRecording::resolve(packet, mPipelines, mGpu.swapchain->getFormat(), mResolved);

    // Before the acquire: the slot's fences show that its semaphores are free to signal again. The
    // waits have no timeout, unlike the acquire.
    const auto frame = mGpu.frames->prepareNextFrame();

    const auto acquireResult =
        mGpu.swapchain->acquireNextImage(gAcquireTimeout, frame->getAcquireSemaphore());
    if (acquireResult.status == Graphics::Vulkan::Swapchain::AcquireStatus::eOutOfDate)
    {
        if (!applyExtent(mRequestedExtent.load()))
        {
            std::this_thread::sleep_for(gZeroExtentBackoff);
        }
        return;
    }
    if (acquireResult.status == Graphics::Vulkan::Swapchain::AcquireStatus::eSkip)
    {
        // The acquire waited up to gAcquireTimeout, so this does not spin.
        return;
    }

    const auto& acquired = acquireResult.acquired;
    VOG_ASSERT_MSG(acquired, "eReady acquire must provide AcquiredSwapchainImage.");

    const vk::Extent2D extent = acquired->getExtent2D();

    // The slot's fences were waited in prepareNextFrame(), so no draw of an earlier frame reads
    // this buffer any more.
    const Graphics::Vulkan::HeapBufferPtr& frameBlock = mGpu.frameBlocks.of(*frame);
    DrawRecording::writeFrameBlock(*frameBlock, packet, extent);

    // Advance the frame slot only when we will actually submit: skip/out-of-date must not burn a
    // fence-keyed release slot. Its pools were reset in prepareNextFrame().
    mGpu.frames->advance();
    auto pool = frame->getCommandBufferPoolForThread(gThreadId);

    auto recorder = pool->record();
    recorder.bindDescriptorHeap(mGpu.heap);

    // The copies that earlier iterations submitted on a queue family of their own are taken over
    // here, before the pass that draws them.
    mUploads.acquireCopied(recorder);

    {
        recorder.setImageBarrier(
            acquired,
            {.srcStageMask        = vk::PipelineStageFlagBits2::eBottomOfPipe,
             .dstStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
             .dstAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
             .oldLayout           = vk::ImageLayout::eUndefined,
             .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .subresourceRange    = {
                 .aspectMask     = vk::ImageAspectFlagBits::eColor,
                 .baseMipLevel   = 0,
                 .levelCount     = VK_REMAINING_MIP_LEVELS,
                 .baseArrayLayer = 0,
                 .layerCount     = VK_REMAINING_ARRAY_LAYERS,
             }});

        {
            auto pass = recorder.beginRendering({{.attachment = acquired,
                                                  .loadOp     = vk::AttachmentLoadOp::eClear,
                                                  .storeOp    = vk::AttachmentStoreOp::eStore,
                                                  .clearValue = vk::ClearColorValue{gClearColor}}});

            DrawRecording::recordDraws(pass, packet, mResolved, mPipelines, frameBlock, extent);
        }

        recorder.setImageBarrier(
            acquired,
            {.srcStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
             .srcAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
             .dstStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
             .oldLayout           = vk::ImageLayout::eColorAttachmentOptimal,
             .newLayout           = vk::ImageLayout::ePresentSrcKHR,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .subresourceRange    = {
                 .aspectMask     = vk::ImageAspectFlagBits::eColor,
                 .baseMipLevel   = 0,
                 .levelCount     = VK_REMAINING_MIP_LEVELS,
                 .baseArrayLayer = 0,
                 .layerCount     = VK_REMAINING_ARRAY_LAYERS,
             }});
    }

    auto executable = recorder.finish();

    // Every graphics submit waits for the last copy, not only one that follows a copy: a draw of
    // a resource that an earlier iteration copied is ordered after that copy by this wait.
    mGpu.graphicsQueue->submit(*mGpu.fencePool,
                               {.waits          = std::array{Graphics::Vulkan::Queue::SemaphoreWait{
                                    .semaphore = acquired->imageAvailableSemaphore(),
                                    .stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
                                .commandBuffers = std::span{&executable, 1u},
                                .signals        = std::array{frame->getRenderFinishedSemaphore()},
                                .timelineWaits  = std::array{Graphics::Vulkan::Queue::TimelineWait{
                                    .semaphore = mUploads.timeline(),
                                    .value     = mUploads.value(),
                                    .stage     = vk::PipelineStageFlagBits2::eAllCommands}}});

    auto presented =
        mGpu.swapchain->present(acquired, frame->getRenderFinishedSemaphore(), *mGpu.fencePool);
    frame->retainUntilPresented(std::move(presented.presentation));

    if (presented.status == Graphics::Vulkan::Swapchain::PresentStatus::eOutOfDate &&
        !applyExtent(mRequestedExtent.load()))
    {
        std::this_thread::sleep_for(gZeroExtentBackoff);
    }
}
} // namespace VOG::Engine
