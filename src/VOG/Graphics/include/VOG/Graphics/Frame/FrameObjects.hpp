#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

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

    FrameObjects(FrameObjects&&)      = default;
    FrameObjects(const FrameObjects&) = delete;

    ~FrameObjects();

    /**
     * Get the command buffer pool by thread id.
     *
     * @param threadId Index of the thread requesting the command buffer pool.
     * Id is simply an index from 0 to @p mCommandBufferPools size.
     *
     * @note Access is not guarded. Engine currently uses threadCount = 1
     * (threadId 0 only); multi-threaded recording is reserved.
     */
    const vk::raii::Semaphore& getRenderFinishedSemaphore() const;

    Vulkan::CommandBufferPoolPtr getCommandBufferPoolForThread(std::size_t threadId);

protected:
    /**
     * Frame cleanups from previous usage like resetting the command buffers and command pools etc.
     */
    void onFrameStart();

    /**
     * Wait for command buffers to complete and reset the pools.
     */
    void resetPools();

    /** Vulkan device provider */
    Vulkan::DevicePtr mDevice;

    /** Signaled by the graphics submit; waited by vkQueuePresentKHR. Safe to reuse after the
     *  frame fence fires — the fence proves the GPU finished the submit, which means the signal
     *  op completed and the PE's wait has been or will be satisfied before the next submit. */
    vk::raii::Semaphore mRenderFinishedSemaphore;

    /** Per working thread command buffer pools */
    std::vector<Vulkan::CommandBufferPoolPtr> mCommandBufferPools;
};
} // namespace VOG::Graphics::Frame
