#include "VOG/Engine/Renderer/UploadQueue.hpp"

#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/UploadTicket.hpp"
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace VOG::Engine
{
namespace
{
std::uint64_t
bytesOf(const UploadJob& job)
{
    return job.staging->byteSize();
}

/** Every handle of the resource is gone: the job holds the last reference, so nothing can draw
 *  the resource, and nothing can get a new handle of it. A count read while another thread drops
 *  a handle can only be too high, which records a copy that no draw needs; it is never too low. */
bool
isDead(const UploadJob& job)
{
    return std::visit(
        [](const auto& destination)
        {
            return destination.use_count() == 1;
        },
        job.destination);
}
} // namespace

UploadQueue::UploadQueue(const Graphics::Vulkan::DevicePtr&     device,
                         Graphics::Vulkan::FencePoolPtr         fencePool,
                         Graphics::Vulkan::TimelineSemaphorePtr timeline,
                         FramePacketQueue&                      wake,
                         std::uint64_t                          backgroundBudget)
    : mTransferQueue{device->getTransferQueue()}
    , mGraphicsQueue{device->getGraphicsQueue()}
    , mFencePool{std::move(fencePool)}
    , mTimeline{std::move(timeline)}
    , mWake{wake}
    , mBackgroundBudget{backgroundBudget}
{
    for (auto& pool : mRing)
    {
        pool = device->createCommandBufferPool(mTransferQueue->familyIndex);
    }
}

void
UploadQueue::push(UploadJob job)
{
    // Before the job is visible, so that the render thread never takes away what was not added.
    mPendingCount.fetch_add(1u, std::memory_order_relaxed);
    mPendingBytes.fetch_add(bytesOf(job), std::memory_order_relaxed);

    {
        const std::scoped_lock lock{mMutex};
        mArrived.push_back(std::move(job));
    }

    mWake.markUploads();
}

Renderer::UploadStatus
UploadQueue::status() const noexcept
{
    return {.pendingCount = mPendingCount.load(std::memory_order_relaxed),
            .pendingBytes = mPendingBytes.load(std::memory_order_relaxed)};
}

void
UploadQueue::takeArrived()
{
    // Moved out under the lock, never copied: a copy would hold a second reference to the
    // destination, and isDead() would never see the last one.
    std::vector<UploadJob> arrived;
    {
        const std::scoped_lock lock{mMutex};
        arrived = std::exchange(mArrived, {});
    }

    std::ranges::move(arrived, std::back_inserter(mHeld));
}

bool
UploadQueue::drain()
{
    takeArrived();

    std::vector<UploadJob> recorded;
    std::vector<UploadJob> kept;
    std::uint64_t          background = 0u;
    for (UploadJob& job : std::exchange(mHeld, {}))
    {
        if (isDead(job))
        {
            mPendingCount.fetch_sub(1u, std::memory_order_relaxed);
            mPendingBytes.fetch_sub(bytesOf(job), std::memory_order_relaxed);
        }
        else if (job.ticket->referenced)
        {
            // A draw must never read a buffer that was not copied: this one is not bounded.
            recorded.push_back(std::move(job));
        }
        else if (background < mBackgroundBudget)
        {
            background += bytesOf(job);
            recorded.push_back(std::move(job));
        }
        else
        {
            kept.push_back(std::move(job));
        }
    }
    mHeld = std::move(kept);

    if (recorded.empty())
    {
        // What is left waits for a draw (a budget of 0 records no background job): another
        // iteration would find the same, so the thread must not run one without a wake.
        return false;
    }

    const std::uint32_t graphicsFamily = mGraphicsQueue->familyIndex;

    const auto& pool = mRing[std::exchange(mNextPool, (mNextPool + 1u) % mRing.size())];
    // Waits for the submit of this pool two iterations ago.
    pool->reset();

    auto recorder = pool->record();
    for (const UploadJob& job : recorded)
    {
        if (const auto* buffer = std::get_if<Graphics::Vulkan::BufferPtr>(&job.destination))
        {
            recorder.copyBuffer(job.staging, *buffer, graphicsFamily);
        }
        else
        {
            recorder.copyBufferToTexture(job.staging,
                                         std::get<Graphics::Vulkan::TexturePtr>(job.destination),
                                         graphicsFamily);
        }
    }
    auto executable = recorder.finish();

    // The value is committed once the submit returned, so a submit that throws leaves it as it
    // was. The submit waits for the one before it: the two pools do not order their copies.
    const std::uint64_t next = mValue + 1u;
    mTransferQueue->submit(*mFencePool,
                           {.commandBuffers  = std::span{&executable, 1u},
                            .timelineWaits   = std::array{Graphics::Vulkan::Queue::TimelineWait{
                                .semaphore = mTimeline,
                                .value     = mValue,
                                .stage     = vk::PipelineStageFlagBits2::eAllCommands}},
                            .timelineSignals = std::array{Graphics::Vulkan::Queue::TimelineSignal{
                                .semaphore = mTimeline, .value = next}}});
    mValue = next;

    // The command buffer holds the staging and the destination now, so the jobs can go.
    const bool otherFamily = mTransferQueue->familyIndex != graphicsFamily;
    for (UploadJob& job : recorded)
    {
        job.ticket->value.store(next, std::memory_order_release);
        mPendingCount.fetch_sub(1u, std::memory_order_relaxed);
        mPendingBytes.fetch_sub(bytesOf(job), std::memory_order_relaxed);
        if (otherFamily)
        {
            mCopied.push_back(std::move(job.destination));
        }
    }

    return !mHeld.empty();
}

void
UploadQueue::acquireCopied(Graphics::Vulkan::CommandBufferRecorder& recorder)
{
    const std::uint32_t transferFamily = mTransferQueue->familyIndex;
    for (const Copied& copied : std::exchange(mCopied, {}))
    {
        std::visit(
            [&](const auto& resource)
            {
                recorder.acquire(resource, transferFamily);
            },
            copied);
    }
}
} // namespace VOG::Engine
