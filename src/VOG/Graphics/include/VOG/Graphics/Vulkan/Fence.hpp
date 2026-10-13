#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/DeviceChild.hpp>

#include <concepts>
#include <functional>
#include <type_traits>

namespace VOG::Graphics::Vulkan
{
/**
 * Binary fence that knows whether the GPU may still signal it, so a wait on a fence that was
 * never handed to the GPU returns at once instead of hanging. Only FencePool creates one, and the
 * VkFence never leaves the class: it goes to the GPU through arm().
 */
class Fence
    : private DeviceChild
    , private vk::raii::Fence
{
    friend class FencePool;

    enum class State : std::uint8_t
    {
        eNotArmed,
        eArmed,
        eArmedAndWaitTimeout,
        eArmedAndWaited
    };

    explicit Fence(DevicePtr device);

public:
    /**
     * Hands the VkFence to @p enqueue, which passes it to a submit or present call. The fence is
     * pending only if @p enqueue returned: a fence that is armed but never handed to the GPU would
     * never signal and its wait would hang.
     *
     * @return what @p enqueue returned.
     */
    template <std::invocable<vk::Fence> Enqueue>
    std::invoke_result_t<Enqueue, vk::Fence>
    arm(Enqueue&& enqueue)
    {
        VOG_ASSERT_MSG(mState == State::eNotArmed, "A fence is armed once per use.");

        if constexpr (std::is_void_v<std::invoke_result_t<Enqueue, vk::Fence>>)
        {
            std::invoke(std::forward<Enqueue>(enqueue), **this);
            mState = State::eArmed;
        }
        else
        {
            auto result = std::invoke(std::forward<Enqueue>(enqueue), **this);
            mState      = State::eArmed;
            return result;
        }
    }

    /**
     * Waits until the GPU signaled the fence; returns eSuccess at once if it was never armed.
     *
     * @param timeout Amount of time to wait.
     */
    vk::Result wait(std::uint64_t timeout);

private:
    /** Unsignals the fence for reuse. Only FencePool calls it, when the fence returns to it. */
    void reset();

    State mState = State::eNotArmed;
};
} // namespace VOG::Graphics::Vulkan
