#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
const std::uint32_t gGrowSize = 10u;
} // namespace

CommandBufferHandle::CommandBufferHandle(CommandBufferPoolPtr pool,
                                         CommandBuffer        commandBuffer) noexcept
    : mPool{std::move(pool)}
    , mCommandBuffer{std::move(commandBuffer)}
{
}

CommandBufferHandle::~CommandBufferHandle()
{
    if (!mPool)
    {
        return;
    }

    mPool->returnCommandBufferToPool(std::move(mCommandBuffer));
}

CommandBuffer*
CommandBufferHandle::operator*() noexcept
{
    return std::addressof(mCommandBuffer);
}

CommandBuffer*
CommandBufferHandle::operator->() noexcept
{
    return std::addressof(mCommandBuffer);
}

CommandBufferHandle::Submission
CommandBufferHandle::submit(Vulkan::TimelineSemaphore::WaitRequest   wait,
                            Vulkan::TimelineSemaphore::SignalRequest signal,
                            const vk::raii::Semaphore&               signalBinary)
{
    VOG_ASSERT(mPool && mCommandBuffer);

    const auto commandBuffers = {*mCommandBuffer};

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

    auto fence = mPool->mFencePool->getShared();

    // Submit work to queue.
    mPool->mDevice->graphicsQueue.submit(submitInfo, **fence->useFence());

    // Add command buffer handle to list of submitted ones.
    mPool->mSubmitedCommandBuffers.push_back(CommandBufferPool::SubmittedCommandBuffer{
        .commandBuffer = std::move(mCommandBuffer), .fence = fence});

    return Submission{.fence = std::move(fence)};
}

CommandBufferPool::CommandBufferPool(const DevicePtr& device)
    : mDevice{device}
    , mVulkanPool{*mDevice,
                  vk::CommandPoolCreateInfo{.queueFamilyIndex =
                                                mDevice->queueInfos.graphics.familyIndex}}
    , mFencePool{FencePool::create(device)}
{
}

CommandBufferHandle
CommandBufferPool::get(vk::CommandBufferLevel type)
{
    auto& pool = type == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers : mSecondaryBuffers;
    if (pool.empty())
    {
        auto newBuffers = Vulkan::CommandBuffer::create(*mDevice, mVulkanPool, type, gGrowSize);
        for (auto& buffer : newBuffers)
        {
            pool.push_back(std::move(buffer));
        }
    }

    Vulkan::CommandBuffer result = std::move(pool.back());
    pool.pop_back();

    return {shared_from_this(), std::move(result)};
}

void
CommandBufferPool::returnCommandBufferToPool(Vulkan::CommandBuffer commandBuffer)
{
    commandBuffer.reset();
    auto& pool = commandBuffer.type == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers
                                                                        : mSecondaryBuffers;

    pool.push_back(std::move(commandBuffer));
}

void
CommandBufferPool::reset()
{
    for (auto& submission : mSubmitedCommandBuffers)
    {
        const auto waitResult = submission.fence->wait(std::numeric_limits<std::uint64_t>::max());
        VOG_ASSERT(waitResult == vk::Result::eSuccess);

        returnCommandBufferToPool(std::move(submission.commandBuffer));
    }
    mSubmitedCommandBuffers.clear();

    mVulkanPool.reset();
}
} // namespace VOG::Graphics::Vulkan
