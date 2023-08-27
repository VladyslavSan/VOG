#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <VOG/Graphics/Vulkan/Fence.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::size_t gGrowthSize = 10u;
}

FencePool::FenceHandle::FenceHandle(FencePoolPtr fencePool, Fence fence)
    : Fence{std::move(fence)}
    , mFencePool{std::move(fencePool)}
{
}

FencePool::FenceHandle::~FenceHandle() { mFencePool->returnToPool(std::move(*this)); }

FencePool::FencePool(DevicePtr device)
    : mDevice{std::move(device)}
{
}

FencePool::FenceHandle
FencePool::get()
{
    if (mFences.empty())
    {
        for (std::size_t i = 0u; i < gGrowthSize; ++i)
        {
            mFences.emplace_back(mDevice);
        }
    }

    auto fence = std::move(mFences.back());
    mFences.pop_back();

    return {shared_from_this(), std::move(fence)};
}

std::shared_ptr<FencePool::FenceHandle>
FencePool::getShared()
{
    if (mFences.empty())
    {
        for (std::size_t i = 0u; i < gGrowthSize; ++i)
        {
            mFences.emplace_back(mDevice);
        }
    }

    auto fence = std::move(mFences.back());
    mFences.pop_back();

    return std::make_shared<FenceHandle>(shared_from_this(), std::move(fence));
}

void
FencePool::returnToPool(Fence fence)
{
    fence.reset();
    mFences.push_back(std::move(fence));
}
} // namespace VOG::Graphics::Vulkan
