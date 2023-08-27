#include "VOG/Graphics/Frame/FrameObjects.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <ranges>
#include <vector>

namespace VOG::Graphics::Frame
{
FrameObjects::FrameObjects(const Vulkan::DevicePtr& device, std::size_t threadCount)
    : mDevice{device}
    , mFramePresentWaitSemaphore{*mDevice, vk::SemaphoreCreateInfo{}}
    , mTimelineSemaphore{mDevice}
{
    mCommandBufferPools.reserve(threadCount);
    std::generate_n(std::back_inserter(mCommandBufferPools),
                    threadCount,
                    [this]() { return Vulkan::CommandBufferPool::create(mDevice); });
}

FrameObjects::~FrameObjects()
{
    if (*mTimelineSemaphore)
    {
        wait();
    }

    if (!mCommandBufferPools.empty())
    {
        resetPools();
    }
}

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

Vulkan::CommandBufferPool&
FrameObjects::getCommandBufferPoolForThread(std::size_t threadId)
{
    VOG_ASSERT_MSG(threadId < mCommandBufferPools.size(), "Invalid threadId requested.");

    return *mCommandBufferPools[threadId];
}

void
FrameObjects::onFrameStart()
{
    wait();
    resetPools();
}

void
FrameObjects::wait()
{
    const auto timelineValue = mTimelineSemaphore.getCounter();
    // Wait for pending command buffers to finish.
    const auto waitResult =
        mTimelineSemaphore.waitOnCPU(timelineValue, std::numeric_limits<std::uint64_t>::max());

    if (waitResult != vk::Result::eSuccess)
    {
        throw std::runtime_error{
            "FrameObjects::waitAndReset failed to wait for previous frame to finish."};
    }
}

void
FrameObjects::resetPools()
{
    std::ranges::for_each(mCommandBufferPools,
                          [](const Vulkan::CommandBufferPoolPtr& poolPtr) { poolPtr->reset(); });
}
} // namespace VOG::Graphics::Frame
