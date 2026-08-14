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
    CommandBufferHandle(CommandBufferHandle&&) = default;

    ~CommandBufferHandle() noexcept(false);

    vk::CommandBuffer consumeForSubmission(std::shared_ptr<FencePool::FenceHandle> fence);

    /**
     * Raw Vulkan handle for peeking before submit (does not transfer ownership).
     */
    vk::CommandBuffer vkHandle() const;

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

protected:
    CommandBufferPoolPtr mPool;
    CommandBuffer        mCommandBuffer;
};

/**
 * Manages a pool of command buffers.
 *
 * @note
 * Should be used by only a one thread at a time as pool manages growth of the space for command
 * buffers allocated from it and if multiple thead record command buffers allocated from one pool
 * internally it will result in race condition.
 */
class CommandBufferPool : public std::enable_shared_from_this<CommandBufferPool>
{
    friend class CommandBufferHandle;
    friend class Device;

    explicit CommandBufferPool(const DevicePtr& device);

    struct SubmittedCommandBuffer
    {
        CommandBuffer                           commandBuffer;
        std::shared_ptr<FencePool::FenceHandle> fence;
    };

public:
    CommandBufferPool(const CommandBufferPool&) = delete;
    CommandBufferPool(CommandBufferPool&&)      = default;

    /**
     * Retrieves clean command buffer from cache or allocates new if cache is exhausted.
     *
     * @param type Type of the command buffer to return - primary or secondary.
     *
     * @returns A lease on a pooled CommandBuffer (record via CommandBufferRecorder).
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
     * @param commandBuffer Command buffer to return to the pool.
     */
    void returnCommandBufferToPool(CommandBuffer commandBuffer);

    DevicePtr             mDevice;
    vk::raii::CommandPool mVulkanPool;

    std::vector<CommandBuffer> mPrimaryBuffers;
    std::vector<CommandBuffer> mSecondaryBuffers;

    /** A list of used command buffer handles during frame recording */
    std::vector<SubmittedCommandBuffer> mSubmittedCommandBuffers;
};
} // namespace VOG::Graphics::Vulkan
