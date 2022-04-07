#include "VOG/Graphics/Frame/FrameObjects.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>

#include <vector>

namespace VOG::Graphics::Frame
{
FrameObjects::FrameObjects(const GraphicsProvider& graphicsProvider, std::size_t threadCount)
    : mGraphicsProvider{graphicsProvider}
    , mFramePresentWaitSemaphore{mGraphicsProvider.getDevice(), vk::SemaphoreCreateInfo{}}
    , mTimelineSemaphore{mGraphicsProvider.getDevice()}
{
    mThreadObjects.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i)
    {
        mThreadObjects.emplace_back(graphicsProvider);
    }
}

FrameObjects::~FrameObjects() { waitAndReset(); }

const vk::raii::Semaphore&
FrameObjects::getFramePresentWaitSemaphore() const
{
    return mFramePresentWaitSemaphore;
}

Vulkan::TimelineSemaphore&
FrameObjects::getTimelineSemaphore()
{
    return mTimelineSemaphore;
}

const Vulkan::TimelineSemaphore&
FrameObjects::getTimelineSemaphore() const
{
    return mTimelineSemaphore;
}

ThreadObjects&
FrameObjects::getThreadObjects(std::size_t threadId)
{
    VOG_ASSERT_MSG(threadId < mThreadObjects.size(), "Invalid threadId requested.");

    return mThreadObjects[threadId];
}

void
FrameObjects::onFrameStart()
{
    waitAndReset();
}

void
FrameObjects::submit(Vulkan::CommandBufferHandle              handle,
                     Vulkan::TimelineSemaphore::WaitRequest   wait,
                     Vulkan::TimelineSemaphore::SignalRequest signal,
                     const vk::raii::Semaphore&               signalBinary)
{
    const auto commandBuffers = {***handle};

    std::array<vk::Semaphore, 2> signalSemaphores{};
    vk::SubmitInfo               submitInfo{.signalSemaphoreCount = 0u};
    submitInfo.setCommandBuffers(commandBuffers);
    submitInfo.setPSignalSemaphores(signalSemaphores.data());

    vk::TimelineSemaphoreSubmitInfo timeline{};
    if (wait.semaphore != nullptr)
    {
        submitInfo.setWaitSemaphores(**wait.semaphore);
        timeline.setWaitSemaphoreValues(wait.value);
    }

    std::array<std::uint64_t, 2> signalValues{};
    if (signal.semaphore != nullptr)
    {
        signalSemaphores[0] = **signal.semaphore;
        signalValues[0]     = signal.value;
        timeline.setSignalSemaphoreValues(signalValues);

        ++submitInfo.signalSemaphoreCount;
    }

    if ((wait.semaphore != nullptr) || (signal.semaphore != nullptr))
    {
        submitInfo.pNext = &timeline;
    }

    if (*signalBinary)
    {
        signalSemaphores[submitInfo.signalSemaphoreCount] = *signalBinary;
        ++submitInfo.signalSemaphoreCount;
    }

    // Submit work to queue.
    mGraphicsProvider.getGraphicsQueue().submit(submitInfo);

    // Add command buffer handle to list of submitted ones.
    mUsedCommandBuffers.push_back(std::move(handle));
}

void
FrameObjects::waitAndReset()
{
    const auto timelineValue = mTimelineSemaphore.getCurrentWaitValue();
    // Wait for pending command buffers to finish.
    mTimelineSemaphore.waitOnCPU(timelineValue, std::numeric_limits<std::uint64_t>::max());

    // Free used command buffers handles so that they are released back to pool.
    mUsedCommandBuffers.clear();

    // Now reset the thread objects and those will reset the pools.
    std::for_each(mThreadObjects.begin(),
                  mThreadObjects.end(),
                  [](ThreadObjects& threadObject) { threadObject.reset(); });
}
} // namespace VOG::Graphics::Frame
