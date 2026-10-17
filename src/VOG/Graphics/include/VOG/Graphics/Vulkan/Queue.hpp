#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

/**
 * Owned by the Device and never copied. Hold it through Device::getGraphicsQueue(): that handle
 * shares ownership of the Device, so the queue stays valid for as long as it is held.
 */
class Queue : private vk::raii::Queue
{
    friend class Device;

    Queue(const Device&             device,
          std::uint32_t             familyIndex,
          vk::QueueFamilyProperties familyProperties);

public:
    Queue(const Queue&) = delete;

    /** A semaphore the submit waits, and the pipeline stages that wait for it. */
    struct SemaphoreWait
    {
        SemaphorePtr            semaphore;
        vk::PipelineStageFlags2 stage;
    };

    /** A timeline semaphore value the submit waits, and the pipeline stages that wait for it. */
    struct TimelineWait
    {
        TimelineSemaphorePtr    semaphore;
        std::uint64_t           value;
        vk::PipelineStageFlags2 stage;
    };

    /** A timeline semaphore value the submit signals once all its commands are done. */
    struct TimelineSignal
    {
        TimelineSemaphorePtr semaphore;
        std::uint64_t        value;
    };

    /**
     * Submits @p commandBuffers, which the submit consumes, using a fence from @p fencePool. The
     * pool is not owned by Queue or Device; every FenceHandle it hands out retains its pool.
     *
     * The command buffers retain the wait and signal semaphores until their fence signals, so the
     * caller can drop its own references, and they wait in their pools. If the submit throws,
     * nothing was submitted and the caller's command buffers are still theirs to submit or drop.
     *
     * @param commandBuffers  At least one: the semaphores are retained by the command buffers.
     *                        Each was recorded from a pool of this queue's family.
     * @param timelineWaits   Waits on timeline semaphores, beside the binary @p waits.
     * @param timelineSignals Signals of timeline semaphores, beside the binary @p signals. The
     *                        caller owns the values; they must rise with every signal.
     *
     * @note Render thread only. The queue is externally synchronized and the submit parks the
     * command buffers in their pools, which take no lock. The graphics and the transfer queue wrap
     * the same VkQueue when they share a family, so their submits (and the swapchain's present)
     * must not run at the same time either.
     */
    void submit(FencePool&                         fencePool,
                std::span<const SemaphoreWait>     waits,
                std::span<ExecutableCommandBuffer> commandBuffers,
                std::span<const SemaphorePtr>      signals,
                std::span<const TimelineWait>      timelineWaits   = {},
                std::span<const TimelineSignal>    timelineSignals = {}) const;

    const std::uint32_t             familyIndex;
    const vk::QueueFamilyProperties familyProperties;
};
} // namespace VOG::Graphics::Vulkan
