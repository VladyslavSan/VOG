#include "VOG/Graphics/Vulkan/Fence.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <utility>

namespace VOG::Graphics::Vulkan
{
Fence::Fence(DevicePtr device)
    : DeviceChild{std::move(device)}
    , vk::raii::Fence{DeviceChild::device()->raii(), vk::FenceCreateInfo{}}
{
}

vk::Result
Fence::wait(std::uint64_t timeout)
{
    if (mState == State::eArmedAndWaited || mState == State::eNotArmed)
    {
        return vk::Result::eSuccess;
    }

    const vk::Result result = device()->waitForFences({**this}, 1u, timeout);
    mState = result == vk::Result::eSuccess ? State::eArmedAndWaited : State::eArmedAndWaitTimeout;

    return result;
}

void
Fence::reset()
{
    if (mState == State::eNotArmed)
    {
        return;
    }

    device()->resetFences({**this});
    mState = State::eNotArmed;
}
} // namespace VOG::Graphics::Vulkan
