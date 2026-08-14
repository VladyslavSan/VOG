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

class FencePool : public std::enable_shared_from_this<FencePool>
{
    friend class Device;

    explicit FencePool(DevicePtr device);

public:
    class FenceHandle : protected Fence
    {
    public:
        FenceHandle(FencePoolPtr fencePool, Fence fence);

        ~FenceHandle();

        FenceHandle(const FenceHandle&) = delete;
        FenceHandle(FenceHandle&&)      = delete;

        using Fence::useFence;
        using Fence::wait;

    private:
        FencePoolPtr mFencePool;
    };

    FenceHandle get();

    std::shared_ptr<FenceHandle> getShared();

protected:
    friend class FenceHandle;
    void returnToPool(Fence fence);

private:
    DevicePtr          mDevice;
    std::vector<Fence> mFences;
};
} // namespace VOG::Graphics::Vulkan