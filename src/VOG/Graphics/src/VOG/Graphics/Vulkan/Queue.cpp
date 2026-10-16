#include "VOG/Graphics/Vulkan/Queue.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <vector>

namespace VOG::Graphics::Vulkan
{
Queue::Queue(const Device&             device,
             std::uint32_t             _familyIndex,
             vk::QueueFamilyProperties _familyProperties)
    : vk::raii::Queue{device.raii(), _familyIndex, 0u}
    , familyIndex{_familyIndex}
    , familyProperties{_familyProperties}
{
}

void
Queue::submit(FencePool&                         fencePool,
              std::span<const SemaphoreWait>     waits,
              std::span<ExecutableCommandBuffer> commandBuffers,
              std::span<const SemaphorePtr>      signals,
              std::span<const TimelineWait>      timelineWaits,
              std::span<const TimelineSignal>    timelineSignals) const
{
    VOG_ASSERT_MSG(!commandBuffers.empty(),
                   "A submit needs at least one command buffer to retain its semaphores.");

    for (const SemaphoreWait& wait : waits)
    {
        VOG_ASSERT_MSG(wait.semaphore, "A wait needs a semaphore.");
    }
    for (const SemaphorePtr& signal : signals)
    {
        VOG_ASSERT_MSG(signal, "A signal needs a semaphore.");
    }

    for (const TimelineWait& wait : timelineWaits)
    {
        VOG_ASSERT_MSG(wait.semaphore, "A timeline wait needs a semaphore.");
    }
    for (const TimelineSignal& signal : timelineSignals)
    {
        VOG_ASSERT_MSG(signal.semaphore, "A timeline signal needs a semaphore.");
    }

    // The value of a binary semaphore is ignored by Vulkan; a timeline semaphore takes its value.
    std::vector<vk::SemaphoreSubmitInfo> waitInfos;
    waitInfos.reserve(waits.size() + timelineWaits.size());
    for (const SemaphoreWait& wait : waits)
    {
        waitInfos.push_back({.semaphore = **wait.semaphore, .stageMask = wait.stage});
    }
    for (const TimelineWait& wait : timelineWaits)
    {
        waitInfos.push_back(
            {.semaphore = **wait.semaphore, .value = wait.value, .stageMask = wait.stage});
    }

    std::vector<vk::SemaphoreSubmitInfo> signalInfos;
    signalInfos.reserve(signals.size() + timelineSignals.size());
    for (const SemaphorePtr& signal : signals)
    {
        signalInfos.push_back(
            {.semaphore = **signal, .stageMask = vk::PipelineStageFlagBits2::eAllCommands});
    }
    for (const TimelineSignal& signal : timelineSignals)
    {
        signalInfos.push_back({.semaphore = **signal.semaphore,
                               .value     = signal.value,
                               .stageMask = vk::PipelineStageFlagBits2::eAllCommands});
    }

    std::vector<vk::CommandBufferSubmitInfo> commandBufferInfos;
    commandBufferInfos.reserve(commandBuffers.size());
    for (ExecutableCommandBuffer& executable : commandBuffers)
    {
        VOG_ASSERT_MSG(executable.mCommandBuffer, "A submitted command buffer was already used.");
        VOG_ASSERT_MSG(executable.mPool->mFamilyIndex == familyIndex,
                       "A queue submits only command buffers of its own family.");

        commandBufferInfos.push_back({.commandBuffer = executable.mCommandBuffer->vkHandle()});

        // Retain before the submit: the retain can allocate, and once the GPU has the work nothing
        // here may throw. If the submit throws, the command buffer is discarded by its owner and
        // releases these again.
        for (const SemaphoreWait& wait : waits)
        {
            executable.mCommandBuffer->retain(wait.semaphore);
        }
        for (const SemaphorePtr& signal : signals)
        {
            executable.mCommandBuffer->retain(signal);
        }
        for (const TimelineWait& wait : timelineWaits)
        {
            executable.mCommandBuffer->retain(wait.semaphore);
        }
        for (const TimelineSignal& signal : timelineSignals)
        {
            executable.mCommandBuffer->retain(signal.semaphore);
        }
    }

    const vk::SubmitInfo2 submitInfo{
        .waitSemaphoreInfoCount   = static_cast<std::uint32_t>(waitInfos.size()),
        .pWaitSemaphoreInfos      = waitInfos.data(),
        .commandBufferInfoCount   = static_cast<std::uint32_t>(commandBufferInfos.size()),
        .pCommandBufferInfos      = commandBufferInfos.data(),
        .signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos.size()),
        .pSignalSemaphoreInfos    = signalInfos.data(),
    };

    FencePool::FenceHandlePtr fence = fencePool.take();
    fence->arm(
        [&](vk::Fence vkFence)
        {
            vk::raii::Queue::submit2(submitInfo, vkFence);
        });

    // The fence is pending, the GPU owns the work: from here on nothing can throw.
    for (ExecutableCommandBuffer& executable : commandBuffers)
    {
        CommandBufferPool& pool = *executable.mPool;
        pool.park(std::move(executable.mCommandBuffer), fence);
    }
}
} // namespace VOG::Graphics::Vulkan
