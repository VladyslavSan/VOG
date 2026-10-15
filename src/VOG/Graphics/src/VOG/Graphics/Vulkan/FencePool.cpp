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

FencePool::FenceHandle::FenceHandle(FencePoolPtr pool, Fence fence) noexcept
    : mPool{std::move(pool)}
    , mFence{std::move(fence)}
{
}

// inDestructor logs a Vulkan failure; returnToPool does not allocate.
FencePool::FenceHandle::~FenceHandle() noexcept
{
    // Resetting a fence the GPU still has to signal is invalid, and its owners (command buffers,
    // presentations) release what the fence guards right after this destructor.
    inDestructor("Waiting for a fence",
                 [this]
                 {
                     [[maybe_unused]] const vk::Result result =
                         mFence.wait(std::numeric_limits<std::uint64_t>::max());
                 });

    inDestructor("Returning a fence to its pool",
                 [this]
                 {
                     mPool->returnToPool(std::move(mFence));
                 });
}

vk::Result
FencePool::FenceHandle::wait(std::uint64_t timeout)
{
    return mFence.wait(timeout);
}

FencePool::FencePool(DevicePtr device)
    : mDevice{std::move(device)}
{
}

FencePool::FenceHandlePtr
FencePool::take()
{
    if (mFences.empty())
    {
        // Reserve first: the fences are created in this batch, and returnToPool must never need
        // more room than the number of fences that exist.
        mFences.reserve(mCreated + gGrowthSize);
        for (std::size_t i = 0u; i < gGrowthSize; ++i)
        {
            mFences.push_back(makeFence());
            ++mCreated;
        }
    }

    Fence fence = std::move(mFences.back());
    mFences.pop_back();

    return FenceHandlePtr{new FenceHandle{shared_from_this(), std::move(fence)}};
}

void
FencePool::returnToPool(Fence fence)
{
    fence.reset();
    mFences.push_back(std::move(fence));
}

Fence
FencePool::makeFence()
{
    return Fence{mDevice};
}
} // namespace VOG::Graphics::Vulkan
