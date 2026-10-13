#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <optional>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(Device);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Graphics::Frame
{
VOG_DECLARE_PTR(FrameObjects);

class FrameObjects
{
    friend class FrameObjectManager;

public:
    FrameObjects(Vulkan::DevicePtr device, std::size_t threadCount);

    FrameObjects(FrameObjects&&)      = delete;
    FrameObjects(const FrameObjects&) = delete;

    /**
     * Nothing to do by hand: members are destroyed in reverse order, so the presentation goes
     * first and waits for its present fence, then the pools it last holds wait for their
     * submitted command buffers (a pool someone else still holds waits in its own destructor).
     * Every wait is a noexcept inDestructor path.
     */
    ~FrameObjects() = default;

    const Vulkan::SemaphorePtr& getRenderFinishedSemaphore() const;

    /**
     * Keeps @p presentation until this frame slot starts again. The slot's render-finished
     * semaphore is waited by that presentation, so the slot must not signal it again before the
     * presentation fence signalled; onFrameStart() drops the presentation, which waits for the
     * fence.
     *
     * @note Render thread only: dropping a Presentation returns its fence to the pool.
     */
    void retainUntilPresented(Vulkan::Swapchain::Presentation presentation);

    /**
     * Get the command buffer pool by thread id.
     *
     * @param threadId Index of the thread requesting the command buffer pool.
     * Id is simply an index from 0 to @p mCommandBufferPools size.
     *
     * @note Access is not guarded. Engine currently uses threadCount = 1
     * (threadId 0 only); multi-threaded recording is reserved.
     */
    Vulkan::CommandBufferPoolPtr getCommandBufferPoolForThread(std::size_t threadId);

protected:
    /**
     * Frame cleanups from previous usage like resetting the command buffers and command pools, and
     * waiting for the previous presentation of this slot.
     */
    void onFrameStart();

    /**
     * Wait for command buffers to complete and reset the pools.
     */
    void resetPools();

    /** Vulkan device provider */
    Vulkan::DevicePtr mDevice;

    /** Signaled by the graphics submit; waited by vkQueuePresentKHR. The submit fence does not
     *  prove the presentation engine consumed the wait, so the semaphore is safe to signal again
     *  only after mPresentation (and its present fence) was dropped in onFrameStart(). */
    Vulkan::SemaphorePtr mRenderFinishedSemaphore;

    /** Per working thread command buffer pools */
    std::vector<Vulkan::CommandBufferPoolPtr> mCommandBufferPools;

    /** The last presentation of this slot; it holds the swapchain the presentation uses. */
    std::optional<Vulkan::Swapchain::Presentation> mPresentation;
};
} // namespace VOG::Graphics::Frame
