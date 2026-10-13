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

class CommandBufferRecorder;

/**
 * A recorded command buffer, ready for Queue::submit. It holds its pool, so the pool outlives it.
 * Dropping it without submitting is a legal discard: the recording is thrown away, everything it
 * retained is released at once, and the pool recycles the buffer at its next reset().
 */
class ExecutableCommandBuffer
{
    friend class CommandBufferRecorder;
    friend class Queue;

    ExecutableCommandBuffer(CommandBufferPoolPtr           pool,
                            std::unique_ptr<CommandBuffer> commandBuffer);

public:
    ExecutableCommandBuffer(ExecutableCommandBuffer&&) noexcept = default;

    ExecutableCommandBuffer(const ExecutableCommandBuffer&)            = delete;
    ExecutableCommandBuffer& operator=(const ExecutableCommandBuffer&) = delete;
    ExecutableCommandBuffer& operator=(ExecutableCommandBuffer&&)      = delete;

    /**
     * Discards the recording unless it was submitted.
     *
     * @note Render thread only. The pool takes no lock.
     */
    ~ExecutableCommandBuffer() noexcept;

private:
    CommandBufferPoolPtr           mPool;
    std::unique_ptr<CommandBuffer> mCommandBuffer;
};

/**
 * Manages a pool of command buffers for a single recording thread.
 *
 * A buffer is in exactly one place: free (initial state), leased (a recorder or an executable
 * command buffer holds it), dirty (leased and discarded, not yet reset) or submitted (parked with
 * the fence of its submit). reset() waits for the submitted fences, resets the Vulkan pool and
 * makes everything free again.
 *
 * @note One pool must be used by only one thread at a time. FrameObjects can
 * allocate several pools (threadCount), but the engine currently hardcodes
 * threadCount = 1 until a second recorder thread exists.
 */
class CommandBufferPool : public std::enable_shared_from_this<CommandBufferPool>
{
    friend class CommandBufferRecorder;
    friend class ExecutableCommandBuffer;
    friend class Device;
    friend class Queue;

    explicit CommandBufferPool(const DevicePtr& device);

    struct Submitted
    {
        std::unique_ptr<CommandBuffer> commandBuffer;
        /**
         * What keeps the buffer from being freed under the GPU is ~CommandBufferPool's explicit
         * wait: a fence shared with another pool is not the last reference here, so ~FenceHandle
         * does not wait. Declared after the buffer, so it is destroyed first; that only helps
         * when this is the last reference.
         */
        FencePool::FenceHandlePtr fence;
    };

public:
    CommandBufferPool(const CommandBufferPool&) = delete;
    CommandBufferPool(CommandBufferPool&&)      = delete;

    /**
     * Waits for every submitted command buffer, so the buffers are never freed while the GPU runs
     * them.
     *
     * @note Render thread only.
     */
    ~CommandBufferPool() noexcept;

    /**
     * The only way to lease a command buffer: the recorder begins it. Dropping the recorder
     * discards the recording.
     */
    [[nodiscard]] CommandBufferRecorder record();

    /**
     * Waits for every submitted fence, resets the Vulkan pool and makes every buffer free.
     * No recorder or executable command buffer of this pool may be alive.
     */
    void reset();

private:
    /** A leased buffer that will not be submitted: released now, recycled at reset(). */
    void discard(std::unique_ptr<CommandBuffer> commandBuffer) noexcept;

    /** discard() for a buffer whose lease was never counted (its begin() threw). */
    void abandon(std::unique_ptr<CommandBuffer> commandBuffer) noexcept;

    /** A leased buffer that was submitted: kept until @p fence signals. */
    void park(std::unique_ptr<CommandBuffer> commandBuffer, FencePool::FenceHandlePtr fence);

    /** Allocates a batch of command buffers. The vectors below get room for all of them first. */
    void grow();

    DevicePtr mDevice;

    /** Declared before the buffers: members are destroyed in reverse, so the buffers are freed
     *  while the Vulkan pool still exists. */
    vk::raii::CommandPool mVulkanPool;

    /** Each buffer is in one of these or leased, so each vector holds at most mAllocated entries.
     *  They are reserved for that in grow(), so discard() and park() never allocate. */
    std::vector<std::unique_ptr<CommandBuffer>> mFree;
    std::vector<std::unique_ptr<CommandBuffer>> mDirty;
    std::vector<Submitted>                      mSubmitted;

    std::size_t mAllocated = 0u;
    std::size_t mLeased    = 0u;
};
} // namespace VOG::Graphics::Vulkan
