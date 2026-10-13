#include "VOG/Graphics/Vulkan/Fence.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <utility>

namespace VOG::Graphics::Vulkan
{
Fence::Fence(DevicePtr device)
    : DeviceChild{std::move(device)}
    , vk::raii::Fence{*DeviceChild::device(), vk::FenceCreateInfo{}}
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

    const vk::Result result = device()->waitForFences({**this}, 1u, timeout);
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

    device()->resetFences({**this});
    mState = State::eNotUsed;
}

vk::Fence
Fence::handle() const
{
    return **this;
}

void
Fence::markUsed()
{
    VOG_ASSERT(mState == State::eNotUsed);

    mState = State::eUsed;
}

} // namespace VOG::Graphics::Vulkan