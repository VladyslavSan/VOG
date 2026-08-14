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

class FencePool
{
    friend class Device;

    /**
     * @param device Owning device. The pool is owned by the device, so a plain reference is
     *               enough; handed out fences retain a DevicePtr instead.
     */
    explicit FencePool(Device& device);

public:
    class FenceHandle : protected Fence
    {
    public:
        explicit FenceHandle(Fence fence);

        ~FenceHandle();

        FenceHandle(const FenceHandle&) = delete;
        FenceHandle(FenceHandle&&)      = delete;

        using Fence::useFence;
        using Fence::wait;
    };

    FenceHandle get();

    std::shared_ptr<FenceHandle> getShared();

protected:
    friend class FenceHandle;
    void returnToPool(Fence fence);

private:
    Fence take();

    Device& mDevice;

    /**
     * Idle fences, stored without their device reference: the pool is owned by the device, so
     * retaining it here would keep the device alive forever.
     */
    std::vector<vk::raii::Fence> mFences;
};
} // namespace VOG::Graphics::Vulkan
