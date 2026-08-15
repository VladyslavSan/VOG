#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

/**
 * Fence with additional state checks so that things like wait operation on not used fence never
 * happens.
 */
class Fence : protected vk::raii::Fence
{
    /**
     * Describes possible Fence states.
     */
    enum class State : std::uint8_t
    {
        eNotUsed,
        eUsed,
        eUsedAndWaitTimeout,
        eUsedAndWaited
    };

private:
    friend class Device;
    friend class FencePool;

    /**
     * @param device Vulkan device to use for fence construction.
     */
    Fence(DevicePtr device);

    /**
     * Adopts an already created fence handle, e.g. an idle one taken from a FencePool.
     *
     * @param device Vulkan device that owns @p fence.
     * @param fence  Fence handle in the unsignaled state.
     */
    Fence(DevicePtr device, vk::raii::Fence fence);

    /**
     * Relinquishes the fence handle so it can be stored without retaining the device.
     *
     * @return The fence handle; this fence is left empty.
     */
    vk::raii::Fence takeHandle();

public:
    /*
     * Wait for fence to be signaled.
     * @param timeout Amount of time to wait.
     */
    vk::Result wait(std::uint64_t timeout);

    /**
     * Reset the fence.
     * Mark it as not used and remove signaled state.
     */
    void reset();

    /*
     * Mark fence as used and extract the Vulkan fence handle.
     * @return Vulkan fence handle.
     */
    const vk::raii::Fence* useFence();

protected:
    DevicePtr mDevice;
    State     mState;
};
} // namespace VOG::Graphics::Vulkan