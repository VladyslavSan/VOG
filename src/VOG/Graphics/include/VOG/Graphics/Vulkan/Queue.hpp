#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Common/Concepts.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <ranges>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

/**
 * Owned by the Device and never copied. Hold it through Device::getGraphicsQueue(): that handle
 * shares ownership of the Device, so the queue stays valid for as long as it is held.
 */
class Queue : public vk::raii::Queue
{
protected:
    friend class Device;

    Queue(const Device&             device,
          std::uint32_t             familyIndex,
          vk::QueueFamilyProperties familyProperties);

public:
    Queue(const Queue&) = delete;

    struct WaitSemaphoreStage
    {
        vk::Semaphore          semaphore;
        vk::PipelineStageFlags stageFlags;
    };

    struct SubmitInfo
    {
        std::vector<WaitSemaphoreStage>  waitInfo;
        std::vector<CommandBufferHandle> commandBuffers;
        std::vector<vk::Semaphore>       signalSemaphores;
    };

    struct Submission
    {
        std::shared_ptr<FencePool::FenceHandle> fence;
    };

    /**
     * Submits command buffers using a fence from @p fencePool. The pool is not owned by Queue or
     * Device; every FenceHandle it hands out retains its pool.
     *
     * Once the submit succeeded, every submitted command buffer retains the wait and signal
     * semaphores until its fence signals, so the caller can drop its own references.
     *
     * @note Render thread only. The queue is externally synchronized and the submit parks the
     * command buffers in their pools, which take no lock.
     */
    Submission
    submit(const FencePoolPtr&                                                fencePool,
           const Common::ContiguousSizedRangeOf<SemaphorePtr> auto&           waitSemaphores,
           const Common::ContiguousSizedRangeOf<vk::PipelineStageFlags> auto& waitDstStageMasks,
           Common::ContiguousSizedRangeOf<CommandBufferHandle> auto           commandBuffers,
           const Common::ContiguousSizedRangeOf<SemaphorePtr> auto& signalSemaphores) const
    {
        // Without a command buffer nothing could retain the semaphores.
        VOG_ASSERT_MSG(std::ranges::size(commandBuffers) > 0u,
                       "A submit needs at least one command buffer to retain its semaphores.");

        auto fence = fencePool->getShared();

        const auto toHandles = [](const auto& semaphores)
        {
            std::vector<vk::Semaphore> handles;
            handles.reserve(std::ranges::size(semaphores));
            for (const SemaphorePtr& semaphore : semaphores)
            {
                handles.push_back(**semaphore);
            }
            return handles;
        };
        const std::vector<vk::Semaphore> waitHandles   = toHandles(waitSemaphores);
        const std::vector<vk::Semaphore> signalHandles = toHandles(signalSemaphores);

        std::vector<vk::CommandBuffer> commandBuffersTmp;
        commandBuffersTmp.reserve(commandBuffers.size());
        for (CommandBufferHandle& handle : commandBuffers)
        {
            commandBuffersTmp.push_back(handle.unsafeVkHandle());
        }

        vk::SubmitInfo submitInfo{
            .waitSemaphoreCount   = static_cast<std::uint32_t>(waitHandles.size()),
            .pWaitSemaphores      = waitHandles.data(),
            .pWaitDstStageMask    = std::ranges::data(waitDstStageMasks),
            .commandBufferCount   = static_cast<std::uint32_t>(commandBuffers.size()),
            .pCommandBuffers      = std::ranges::data(commandBuffersTmp),
            .signalSemaphoreCount = static_cast<std::uint32_t>(signalHandles.size()),
            .pSignalSemaphores    = signalHandles.data(),
        };

        vk::raii::Queue::submit(submitInfo, fence->handle());
        // Only a submitted fence will signal; marking it earlier would hang its destructor if
        // the submit threw.
        fence->markUsed();

        for (CommandBufferHandle& handle : commandBuffers)
        {
            for (const SemaphorePtr& semaphore : waitSemaphores)
            {
                handle->addBoundResource(semaphore);
            }
            for (const SemaphorePtr& semaphore : signalSemaphores)
            {
                handle->addBoundResource(semaphore);
            }
            handle.consumeForSubmission(fence);
        }

        return Submission{.fence = std::move(fence)};
    }

    const std::uint32_t             familyIndex;
    const vk::QueueFamilyProperties familyProperties;
};
} // namespace VOG::Graphics::Vulkan
