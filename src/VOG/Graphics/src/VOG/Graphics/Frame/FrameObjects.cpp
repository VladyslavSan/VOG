#include "VOG/Graphics/Frame/FrameObjects.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <ranges>
#include <vector>

namespace VOG::Graphics::Frame
{
FrameObjects::FrameObjects(Vulkan::DevicePtr device, std::size_t threadCount)
    : mDevice{std::move(device)}
    , mAcquireSemaphore{mDevice->createSemaphore()}
    , mRenderFinishedSemaphore{mDevice->createSemaphore()}
    , mTransferCommandBufferPool{
          mDevice->createCommandBufferPool(mDevice->queueInfos.transfer.familyIndex)}
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
FrameObjects::getAcquireSemaphore() const
{
    return mAcquireSemaphore;
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

    // emplace destroys the old presentation, waiting for its fence, before it builds the new one;
    // an assignment would replace the generation first.
    mPresentation.emplace(std::move(presentation));
}

Vulkan::CommandBufferPoolPtr
FrameObjects::getCommandBufferPoolForThread(std::size_t threadId)
{
    VOG_ASSERT_MSG(threadId < mCommandBufferPools.size(), "Invalid threadId requested.");

    return mCommandBufferPools[threadId];
}

const Vulkan::CommandBufferPoolPtr&
FrameObjects::getTransferCommandBufferPool() const
{
    return mTransferCommandBufferPool;
}

void
FrameObjects::onFrameStart()
{
    resetPools();

    // The presentation engine must be done with the render-finished semaphore before the next
    // submit signals it again. Dropping the presentation waits for its present fence.
    mPresentation.reset();
}

void
FrameObjects::resetPools()
{
    std::ranges::for_each(mCommandBufferPools,
                          [](const Vulkan::CommandBufferPoolPtr& poolPtr)
                          {
                              poolPtr->reset();
                          });
    mTransferCommandBufferPool->reset();
}
} // namespace VOG::Graphics::Frame
