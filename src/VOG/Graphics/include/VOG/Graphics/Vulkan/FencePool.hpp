#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

#include <memory>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(Fence);
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

    class FenceHandle : protected Fence
    {
    public:
        FenceHandle(FencePoolPtr pool, Fence fence);

        ~FenceHandle();

        FenceHandle(const FenceHandle&) = delete;
        FenceHandle(FenceHandle&&)      = delete;

        using Fence::useFence;
        using Fence::wait;

    private:
        FencePoolPtr mPool;
    };

    std::shared_ptr<FenceHandle> getShared();

protected:
    friend class FenceHandle;
    void returnToPool(Fence fence);

private:
    Fence take();

    DevicePtr mDevice;

    std::vector<Fence> mFences;
};
} // namespace VOG::Graphics::Vulkan
