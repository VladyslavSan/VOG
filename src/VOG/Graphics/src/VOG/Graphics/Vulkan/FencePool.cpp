#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

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

// wait() throws on device loss and returnToPool() on allocation failure. Either one escaping a
// destructor terminates, which is the right answer for a lost device or no memory.
FencePool::FenceHandle::~FenceHandle() // NOLINT(bugprone-exception-escape)
{
    // Resetting a fence the GPU still has to signal is invalid, and its owners (command buffers,
    // presentations) release what the fence guards right after this destructor.
    [[maybe_unused]] const vk::Result waitResult = wait(std::numeric_limits<std::uint64_t>::max());
    VOG_ASSERT(waitResult == vk::Result::eSuccess);

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
