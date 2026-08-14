#pragma once

#include <VOG/Common/Concepts.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Queue : public vk::raii::Queue
{
protected:
    friend class Device;

    Queue(const Device&             device,
          std::uint32_t             familyIndex,
          vk::QueueFamilyProperties familyProperties);

public:
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
     * Device; the caller retains it for the lifetime of any in-flight FenceHandle.
     */
    Submission
    submit(FencePoolPtr                                                       fencePool,
           const Common::ContiguousSizedRangeOf<vk::Semaphore> auto&          waitSemaphores,
           const Common::ContiguousSizedRangeOf<vk::PipelineStageFlags> auto& waitDstStageMasks,
           Common::ContiguousSizedRangeOf<CommandBufferHandle> auto           commandBuffers,
           const Common::ContiguousSizedRangeOf<vk::Semaphore> auto& signalSemaphores) const
    {
        auto fence = fencePool->getShared();

        std::vector<vk::CommandBuffer> commandBuffersTmp;
        commandBuffersTmp.reserve(commandBuffers.size());
        for (CommandBufferHandle& handle : commandBuffers)
        {
            commandBuffersTmp.push_back(handle.consumeForSubmission(fence));
        }

        vk::SubmitInfo submitInfo{
            .waitSemaphoreCount   = static_cast<std::uint32_t>(waitSemaphores.size()),
            .pWaitSemaphores      = std::ranges::data(waitSemaphores),
            .pWaitDstStageMask    = std::ranges::data(waitDstStageMasks),
            .commandBufferCount   = static_cast<std::uint32_t>(commandBuffers.size()),
            .pCommandBuffers      = std::ranges::data(commandBuffersTmp),
            .signalSemaphoreCount = static_cast<std::uint32_t>(signalSemaphores.size()),
            .pSignalSemaphores    = std::ranges::data(signalSemaphores),
        };

        vk::raii::Queue::submit(submitInfo, **fence->useFence());

        return Submission{.fence = std::move(fence)};
    }

    const Device&                   device;
    const std::uint32_t             familyIndex;
    const vk::QueueFamilyProperties familyProperties;
};
} // namespace VOG::Graphics::Vulkan
