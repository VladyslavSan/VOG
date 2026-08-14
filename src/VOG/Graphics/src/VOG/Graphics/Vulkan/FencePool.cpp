#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::size_t gGrowthSize = 10u;
}

FencePool::FenceHandle::FenceHandle(Fence fence)
    : Fence{std::move(fence)}
{
}

FencePool::FenceHandle::~FenceHandle()
{
    // Resolve the pool first; moving the fence out takes mDevice with it.
    FencePool& pool = mDevice->getFencePool();
    pool.returnToPool(std::move(*this));
}

FencePool::FencePool(Device& device)
    : mDevice{device}
{
}

FencePool::FenceHandle
FencePool::get()
{
    return FenceHandle{take()};
}

std::shared_ptr<FencePool::FenceHandle>
FencePool::getShared()
{
    return std::make_shared<FenceHandle>(take());
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
            mFences.emplace_back(mDevice, vk::FenceCreateInfo{});
        }
    }

    auto fence = std::move(mFences.back());
    mFences.pop_back();

    return Fence{mDevice.shared_from_this(), std::move(fence)};
}
} // namespace VOG::Graphics::Vulkan
