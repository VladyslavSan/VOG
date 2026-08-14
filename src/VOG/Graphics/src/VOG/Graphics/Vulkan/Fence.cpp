#include "VOG/Graphics/Vulkan/Fence.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <utility>

namespace VOG::Graphics::Vulkan
{
Fence::Fence(DevicePtr device)
    : vk::raii::Fence{*device, vk::FenceCreateInfo{}}
    , mDevice{std::move(device)}
    , mState{State::eNotUsed}
{
}

Fence::Fence(DevicePtr device, vk::raii::Fence fence)
    : vk::raii::Fence{std::move(fence)}
    , mDevice{std::move(device)}
    , mState{State::eNotUsed}
{
}

vk::raii::Fence
Fence::takeHandle()
{
    return std::move(static_cast<vk::raii::Fence&>(*this));
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