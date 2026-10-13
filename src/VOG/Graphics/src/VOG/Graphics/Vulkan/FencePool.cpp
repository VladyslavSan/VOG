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

// returnToPool can only throw on allocation failure; terminating is the right answer there.
FencePool::FenceHandle::~FenceHandle() // NOLINT(bugprone-exception-escape)
{
    mPool->returnToPool(std::move(*this));
}

FencePool::FencePool(DevicePtr device)
    : mDevice{std::move(device)}
{
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
    mFences.push_back(std::move(fence));
}

Fence
FencePool::take()
{
    if (mFences.empty())
    {
        for (std::size_t i = 0u; i < gGrowthSize; ++i)
        {
            mFences.push_back(mDevice->createFence());
        }
    }

    auto fence = std::move(mFences.back());
    mFences.pop_back();

    return fence;
}
} // namespace VOG::Graphics::Vulkan
