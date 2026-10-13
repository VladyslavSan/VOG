#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

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

    /**
     * Submits @p commandBuffers, which the submit consumes, using a fence from @p fencePool. The
     * pool is not owned by Queue or Device; every FenceHandle it hands out retains its pool.
     *
     * The command buffers retain the wait and signal semaphores until their fence signals, so the
     * caller can drop its own references, and they wait in their pools. If the submit throws,
     * nothing was submitted and the caller's command buffers are still theirs to submit or drop.
     *
     * @param commandBuffers  At least one: the semaphores are retained by the command buffers.
     *
     * @note Render thread only. The queue is externally synchronized and the submit parks the
     * command buffers in their pools, which take no lock.
     */
    void submit(FencePool&                         fencePool,
                std::span<const SemaphoreWait>     waits,
                std::span<ExecutableCommandBuffer> commandBuffers,
                std::span<const SemaphorePtr>      signals) const;

    const std::uint32_t             familyIndex;
    const vk::QueueFamilyProperties familyProperties;
};
} // namespace VOG::Graphics::Vulkan
