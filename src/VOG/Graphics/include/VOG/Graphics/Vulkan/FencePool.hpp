#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(FencePool);

/**
 * Recycles binary fences for queue submissions. Owned by the composition root (e.g. Renderer),
 * not by Device. A FenceHandle retains its pool and every idle fence retains the device, so
 * holding either one keeps the device alive.
 *
 * @note Render thread only. The pool takes no lock, and a FenceHandle returns its fence to the
 * pool when the last reference drops, so the last reference must drop on the thread that uses
 * the pool.
 */
class FencePool : public std::enable_shared_from_this<FencePool>
{
    explicit FencePool(DevicePtr device);

public:
    FencePool(const FencePool&) = delete;
    FencePool(FencePool&&)      = delete;

    /** The pool hands out handles that call shared_from_this(), so it exists only as shared_ptr. */
    static FencePoolPtr
    create(DevicePtr device)
    {
        return std::shared_ptr<FencePool>(new FencePool{std::move(device)});
    }

    /**
     * One fence on loan from the pool. It can be shared: one submit fence guards the command
     * buffers of several pools.
     */
    class FenceHandle
    {
        friend class FencePool;
        friend class Queue;
        friend class Swapchain;

        FenceHandle(FencePoolPtr pool, Fence fence) noexcept;

        /**
         * Hands the fence to @p enqueue and marks it pending only if @p enqueue returned.
         *
         * Not tested directly, by decision: arm is a few lines, only Queue and Swapchain call it,
         * and a surface can only be lost with a hook the tests do not have. The shape is enforced
         * by review, and the present path keeps its fence armed when the present reports
         * SurfaceLost (see Swapchain::present).
         */
        template <std::invocable<vk::Fence> Enqueue>
        auto
        arm(Enqueue&& enqueue)
        {
            return mFence.arm(std::forward<Enqueue>(enqueue));
        }

    public:
        /**
         * Waits for the fence if the GPU may still signal it, then returns it to the pool. The
         * owner of the handle can therefore drop it at any time, and what the fence guards stays
         * alive until the GPU is done.
         *
         * @note Render thread only. The pool takes no lock.
         */
        ~FenceHandle() noexcept;

        FenceHandle(const FenceHandle&) = delete;
        FenceHandle(FenceHandle&&)      = delete;

        /** Returns eSuccess at once for a fence that was never armed. */
        vk::Result wait(std::uint64_t timeout);

    private:
        FencePoolPtr mPool;
        Fence        mFence;
    };

    using FenceHandlePtr = std::shared_ptr<FenceHandle>;

    FenceHandlePtr take();

private:
    friend class FenceHandle;

    /** Never allocates: mFences is reserved for every fence the pool created. */
    void returnToPool(Fence fence);

    Fence makeFence();

    DevicePtr mDevice;

    std::vector<Fence> mFences;

    /** Fences created so far; mFences never needs more room than this. */
    std::size_t mCreated = 0u;
};
} // namespace VOG::Graphics::Vulkan
