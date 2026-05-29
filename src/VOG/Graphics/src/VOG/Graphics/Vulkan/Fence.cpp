#include "VOG/Graphics/Vulkan/Fence.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
Fence::Fence(DevicePtr device)
    : vk::raii::Fence{*device, vk::FenceCreateInfo{}}
    , mDevice{std::move(device)}
    , mState{State::eNotUsed}
{
}

vk::Result
Fence::wait(std::uint64_t timeout)
{
    if (mState == State::eUsedAndWaited || mState == State::eNotUsed)
    {
        return vk::Result::eSuccess;
    }

    const vk::Result result = mDevice->waitForFences({**this}, 1u, timeout);
    mState = result == vk::Result::eSuccess ? State::eUsedAndWaited : State::eUsedAndWaitTimeout;

    return result;
}

void
Fence::reset()
{
    if (mState == State::eNotUsed)
    {
        return;
    }

    mDevice->resetFences({**this});
    mState = State::eNotUsed;
}

const vk::raii::Fence*
Fence::useFence()
{
    VOG_ASSERT(mState == State::eNotUsed);

    mState = State::eUsed;
    return this;
}
} // namespace VOG::Graphics::Vulkan