#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

#include <memory>
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

    CommandBufferHandle(CommandBufferPoolPtr           pool,
                        std::unique_ptr<CommandBuffer> commandBuffer) noexcept;

public:
    CommandBufferHandle(CommandBufferHandle&&) = default;

    /**
     * Returns the command buffer to its pool unless it was submitted.
     *
     * @note Render thread only. The pool takes no lock.
     */
    ~CommandBufferHandle() noexcept(false);

    /**
     * Parks the command buffer in the pool until @p fence signals. Called by Queue::submit after
     * the submit succeeded.
     */
    void consumeForSubmission(std::shared_ptr<FencePool::FenceHandle> fence);

    /**
     * Raw Vulkan handle for submit. Recording through it bypasses resource retention.
     */
    vk::CommandBuffer unsafeVkHandle() const;

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
    CommandBufferPoolPtr           mPool;
    std::unique_ptr<CommandBuffer> mCommandBuffer;
};

/**
 * Manages a pool of command buffers for a single recording thread.
 *
 * @note One pool must be used by only one thread at a time. FrameObjects can
 * allocate several pools (threadCount), but the engine currently hardcodes
 * threadCount = 1 until a second recorder thread exists.
 */
class CommandBufferPool : public std::enable_shared_from_this<CommandBufferPool>
{
    friend class CommandBufferHandle;
    friend class Device;

    explicit CommandBufferPool(const DevicePtr& device);

    struct SubmittedCommandBuffer
    {
        std::unique_ptr<CommandBuffer>          commandBuffer;
        std::shared_ptr<FencePool::FenceHandle> fence;
    };

public:
    CommandBufferPool(const CommandBufferPool&) = delete;
    CommandBufferPool(CommandBufferPool&&)      = delete;

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
    void returnCommandBufferToPool(std::unique_ptr<CommandBuffer> commandBuffer);

    DevicePtr             mDevice;
    vk::raii::CommandPool mVulkanPool;

    std::vector<std::unique_ptr<CommandBuffer>> mPrimaryBuffers;
    std::vector<std::unique_ptr<CommandBuffer>> mSecondaryBuffers;

    /** A list of used command buffer handles during frame recording */
    std::vector<SubmittedCommandBuffer> mSubmittedCommandBuffers;
};
} // namespace VOG::Graphics::Vulkan
