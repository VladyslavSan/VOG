#include "VOG/Graphics/Frame/FrameObjects.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <limits>
#include <ranges>
#include <vector>

namespace VOG::Graphics::Frame
{
FrameObjects::FrameObjects(Vulkan::DevicePtr device, std::size_t threadCount)
    : mDevice{std::move(device)}
    , mRenderFinishedSemaphore{mDevice->createSemaphore()}
{
    mCommandBufferPools.reserve(threadCount);
    std::generate_n(std::back_inserter(mCommandBufferPools),
                    threadCount,
                    [this]()
                    {
                        return mDevice->createCommandBufferPool();
                    });
}

const Vulkan::SemaphorePtr&
FrameObjects::getRenderFinishedSemaphore() const
{
    return mRenderFinishedSemaphore;
}

void
FrameObjects::retainUntilPresented(Vulkan::Swapchain::Presentation presentation)
{
    VOG_ASSERT_MSG(!mPresentation, "The previous presentation of this slot was not dropped.");

    mPresentation = std::move(presentation);
}

Vulkan::CommandBufferPoolPtr
FrameObjects::getCommandBufferPoolForThread(std::size_t threadId)
{
    VOG_ASSERT_MSG(threadId < mCommandBufferPools.size(), "Invalid threadId requested.");

    return mCommandBufferPools[threadId];
}

void
FrameObjects::onFrameStart()
{
    resetPools();

    // The presentation engine must be done with the render-finished semaphore before the next
    // submit signals it again, so wait for the present fence here, not in a destructor.
    if (mPresentation)
    {
        [[maybe_unused]] const vk::Result waitResult =
            mPresentation->fence->wait(std::numeric_limits<std::uint64_t>::max());
        VOG_ASSERT(waitResult == vk::Result::eSuccess);

        mPresentation.reset();
    }
}

void
FrameObjects::resetPools()
{
    std::ranges::for_each(mCommandBufferPools,
                          [](const Vulkan::CommandBufferPoolPtr& poolPtr)
                          {
                              poolPtr->reset();
                          });
}
} // namespace VOG::Graphics::Frame
