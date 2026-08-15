#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::size_t gGrowthSize = 10u;
}

FencePool::FenceHandle::FenceHandle(FencePoolPtr pool, Fence fence)
    : Fence{std::move(fence)}
    , mPool{std::move(pool)}
{
}

FencePool::FenceHandle::~FenceHandle()
{
    // Hold the pool before moving the fence out (takeHandle strips DevicePtr).
    const FencePoolPtr pool = mPool;
    pool->returnToPool(std::move(*this));
}

FencePool::FencePool(DevicePtr device)
    : mDevice{std::move(device)}
{
}

FencePool::FenceHandle
FencePool::get()
{
    return FenceHandle{shared_from_this(), take()};
}

std::shared_ptr<FencePool::FenceHandle>
FencePool::getShared()
{
    return std::make_shared<FenceHandle>(shared_from_this(), take());
}

void
FencePool::returnToPool(Fence fence)
{
    fence.reset();
    mFences.push_back(fence.takeHandle());
}

Fence
FencePool::take()
{
    if (mFences.empty())
    {
        for (std::size_t i = 0u; i < gGrowthSize; ++i)
        {
            mFences.emplace_back(*mDevice, vk::FenceCreateInfo{});
        }
    }

    auto fence = std::move(mFences.back());
    mFences.pop_back();

    return Fence{mDevice, std::move(fence)};
}
} // namespace VOG::Graphics::Vulkan
