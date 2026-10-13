#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/DeviceChild.hpp>

namespace VOG::Graphics::Vulkan
{
/**
 * Fence with additional state checks so that things like wait operation on not used fence never
 * happens.
 */
class Fence
    : protected DeviceChild
    , protected vk::raii::Fence
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

    /**
     * @param device Vulkan device to use for fence construction.
     */
    Fence(DevicePtr device);

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

    /**
     * Vulkan fence handle to pass to a submit or present call.
     */
    vk::Fence handle() const;

    /**
     * Mark the fence as used. Call it only after the submit or present call that takes handle()
     * succeeded: a fence marked used but never handed to the GPU would never signal.
     */
    void markUsed();

protected:
    State mState;
};
} // namespace VOG::Graphics::Vulkan