#pragma once

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
     * @note for a thread pool thread ids should be unique as access to thread objects is not
     * guarded.
     */
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

protected:
    /** Vulkan device provider */
    Vulkan::DevicePtr mDevice;

    /** Per working thread command buffer pools */
    std::vector<Vulkan::CommandBufferPoolPtr> mCommandBufferPools;
};
} // namespace VOG::Graphics::Frame
