#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>
#include <VOG/Graphics/Vulkan/InDestructor.hpp>

#include <limits>

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

// inDestructor logs a Vulkan failure. A bad_alloc from push_back in returnToPool is not caught
// there and terminates, which is the right answer for no memory.
FencePool::FenceHandle::~FenceHandle()
{
    // Resetting a fence the GPU still has to signal is invalid, and its owners (command buffers,
    // presentations) release what the fence guards right after this destructor.
    inDestructor("Waiting for a fence",
                 [this]
                 {
                     [[maybe_unused]] const vk::Result result =
                         wait(std::numeric_limits<std::uint64_t>::max());
                 });

    inDestructor("Returning a fence to its pool",
                 [this]
                 {
                     mPool->returnToPool(std::move(*this));
                 });
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
