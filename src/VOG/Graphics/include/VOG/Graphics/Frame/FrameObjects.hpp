#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <span>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(Device);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Graphics::Frame
{
class FrameObjects
{
    friend class FrameObjectManager;

public:
    FrameObjects(const Vulkan::DevicePtr& device, std::size_t threadCount);

    FrameObjects(FrameObjects&&)      = default;
    FrameObjects(const FrameObjects&) = delete;

    ~FrameObjects();

    const vk::raii::Semaphore& getFramePresentSemaphore() const;

    /**
     * Get the command buffer pool by thread id.
     *
     * @param threadId Index of the thread requesting the command buffer pool.
     * Id is simply an index from 0 to @p mCommandBufferPools size.
     *
     * @note for a thread pool thread ids should be unique as access to thread objects is not
     * guarded.
     */
    Vulkan::CommandBufferPool& getCommandBufferPoolForThread(std::size_t threadId);

protected:
    /**
     * Frame cleanups from previous usage like resetting the command buffers and command pools etc.
     */
    void onFrameStart();

    void wait();

    /**
     * Wait for command buffers to complete and reset the pools.
     */
    void resetPools();

protected:
    /** Vulkan device provider */
    Vulkan::DevicePtr mDevice;

    /**
     * Semaphore that will be signaled by last command buffer of the frame and waited on before
     * presenting frame to surface.
     * @note this semaphore should be used carefully as there is no way to reset non timeline
     * semaphore state.
     */
    vk::raii::Semaphore mFramePresentWaitSemaphore;

    /** Per working thread command buffer pools */
    std::vector<Vulkan::CommandBufferPoolPtr> mCommandBufferPools;
};
} // namespace VOG::Graphics::Frame
