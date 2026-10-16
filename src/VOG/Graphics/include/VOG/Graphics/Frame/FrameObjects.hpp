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
     * The semaphore the swapchain acquire of this slot's frame signals and the frame's submit
     * waits. It is unsignaled with no pending operation once FrameObjectManager::prepareNextFrame
     * returned this slot: the wait of the slot's previous submit is done, as its fence showed.
     * An image dropped between acquire and submit leaves it signaled (SwapchainSync.md, known gap).
     */
    const Vulkan::SemaphorePtr& getAcquireSemaphore() const;

    /**
     * Keeps @p presentation until this frame slot starts again. The slot's render-finished
     * semaphore is waited by that presentation, so the slot must not signal it again before the
     * presentation fence signalled; onFrameStart() drops the presentation, and
     * ~Presentation waits for the fence.
     *
     * @note Render thread only: dropping a Presentation returns its fence to the pool.
     */
    void retainUntilPresented(Vulkan::Swapchain::Presentation presentation);

    /**
     * Get the command buffer pool by thread id.
     *
     * @param threadId Index of the thread requesting the command buffer pool.
     * Id is simply an index from 0 to `mCommandBufferPools` size.
     *
     * @note Access is not guarded. Engine currently uses threadCount = 1
     * (threadId 0 only); multi-threaded recording is reserved.
     */
    Vulkan::CommandBufferPoolPtr getCommandBufferPoolForThread(std::size_t threadId);

    /**
     * The pool of the slot for command buffers of the transfer queue's family. Reset together
     * with the other pools when the slot starts again.
     *
     * @note Render thread only, like the other pools.
     */
    const Vulkan::CommandBufferPoolPtr& getTransferCommandBufferPool() const;

protected:
    /**
     * Frame cleanups from previous usage like resetting the command buffers and command pools, and
     * dropping the previous presentation of this slot, whose destructor waits for its present
     * fence. Calling it again before the slot is used changes nothing.
     */
    void onFrameStart();

    /**
     * Wait for command buffers to complete and reset the pools.
     */
    void resetPools();

    /** Vulkan device provider */
    Vulkan::DevicePtr mDevice;

    /** Signaled by the swapchain acquire; waited by the graphics submit. The submit fence, which
     *  onFrameStart() waits, shows that the wait is done, so the next acquire may signal it again.
     */
    Vulkan::SemaphorePtr mAcquireSemaphore;

    /** Signaled by the graphics submit; waited by vkQueuePresentKHR. The submit fence does not
     *  prove the presentation engine consumed the wait, so the semaphore is safe to signal again
     *  only after mPresentation was dropped in onFrameStart(), which waits for its present fence.
     */
    Vulkan::SemaphorePtr mRenderFinishedSemaphore;

    /** Per working thread command buffer pools */
    std::vector<Vulkan::CommandBufferPoolPtr> mCommandBufferPools;

    /** Command buffers of the transfer queue's family; the graphics family when the device has no
     *  family of its own for transfer. */
    Vulkan::CommandBufferPoolPtr mTransferCommandBufferPool;

    /** The last presentation of this slot; it holds the swapchain the presentation uses. */
    std::optional<Vulkan::Swapchain::Presentation> mPresentation;
};
} // namespace VOG::Graphics::Frame
