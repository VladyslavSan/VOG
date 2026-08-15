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
 * not by Device. Live handles retain FencePoolPtr → DevicePtr; idle entries store bare fences.
 */
class FencePool : public std::enable_shared_from_this<FencePool>
{
public:
    explicit FencePool(DevicePtr device);

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

    FenceHandle get();

    std::shared_ptr<FenceHandle> getShared();

    const DevicePtr&
    device() const
    {
        return mDevice;
    }

protected:
    friend class FenceHandle;
    void returnToPool(Fence fence);

private:
    Fence take();

    DevicePtr mDevice;

    /** Idle fences without DevicePtr, so the pool does not pin the device while empty. */
    std::vector<vk::raii::Fence> mFences;
};
} // namespace VOG::Graphics::Vulkan
