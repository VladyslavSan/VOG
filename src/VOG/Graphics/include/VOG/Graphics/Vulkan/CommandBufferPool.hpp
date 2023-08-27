#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(CommandBufferPool);
VOG_DECLARE_PTR(FencePool);
VOG_DECLARE_PTR(Fence);

/**
 * A handle to command buffer that is bound to CommandBufferPool. It can be recorded and then
 * submitted for execution via CommandBufferHandle::submit() method or simply drop it if execution
 * is not desired (if recorded commands in the command buffer should be discarded).
 */
class CommandBufferHandle
{
    friend class CommandBufferPool;

    CommandBufferHandle(CommandBufferPoolPtr pool, CommandBuffer commandBuffer) noexcept;

public:
    /**
     * Submission.
     */
    struct Submission
    {
        const std::shared_ptr<FencePool::FenceHandle> fence;
    };

    struct SubmitInfo
    {
        std::shared_ptr<Fence> signalFence;
    };

    CommandBufferHandle(CommandBufferHandle&&) = default;

    ~CommandBufferHandle();

    /**
     * Allows usage of the handle as if it's an instance of the CommandBuffer.
     * @return CommandBuffer handle.
     */
    CommandBuffer* operator*() noexcept;

    /**
     * Allows usage of the handle as if it's an instance of the CommandBuffer.
     * @return CommandBuffer handle.
     */
    CommandBuffer* operator->() noexcept;

    /**
     * Function for command buffer submission.
     *
     * @param wait Wait request on semaphore.
     * @param signal Signal request on semaphore.
     * @param signalBinary Additional binary semaphore for signaling, mostly to connect it with
     * surface present command as it only accepts binary semaphores
     *
     * @returns submission object.
     */
    CommandBufferHandle::Submission submit(Vulkan::TimelineSemaphore::WaitRequest   wait,
                                           Vulkan::TimelineSemaphore::SignalRequest signal,
                                           const vk::raii::Semaphore&               signalBinary);

protected:
    CommandBufferPoolPtr mPool;
    CommandBuffer        mCommandBuffer;
};

/**
 * Manages command buffers in a pool, submissions of those and cleanup.
 */
class CommandBufferPool : public std::enable_shared_from_this<CommandBufferPool>
{
    friend class CommandBufferHandle;
    CommandBufferPool(const DevicePtr& device);

    struct SubmittedCommandBuffer
    {
        CommandBuffer                           commandBuffer;
        std::shared_ptr<FencePool::FenceHandle> fence;
    };

public:
    CommandBufferPool(const CommandBufferPool&) = delete;
    CommandBufferPool(CommandBufferPool&&)      = default;

    static std::shared_ptr<CommandBufferPool>
    create(const DevicePtr& device)
    {
        return std::shared_ptr<CommandBufferPool>{new CommandBufferPool{device}};
    }

    /**
     * Retrieves clean command buffer from cache or allocates new if cache is exhausted.
     *
     * @param type Type of the command buffer to return - primary or secondary.
     *
     * @returns CommandBufferRecorder instance.
     */
    CommandBufferHandle get(CommandBufferType type);

    /**
     * Reset command buffer pool.
     */
    void reset();

protected:
    /**
     * Return command buffer back to the pool.
     *
     * @param submission Command buffer to return to the pool.
     */
    void returnCommandBufferToPool(CommandBuffer submission);

protected:
    DevicePtr             mDevice;
    vk::raii::CommandPool mVulkanPool;

    std::vector<CommandBuffer> mPrimaryBuffers;
    std::vector<CommandBuffer> mSecondaryBuffers;

    /** A list of used command buffer handles during frame recording */
    std::vector<SubmittedCommandBuffer> mSubmitedCommandBuffers;
    FencePoolPtr                        mFencePool;
};
} // namespace VOG::Graphics::Vulkan
