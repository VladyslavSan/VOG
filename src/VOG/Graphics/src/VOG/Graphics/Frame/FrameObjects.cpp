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
    , mRenderFinishedSemaphore{*mDevice, vk::SemaphoreCreateInfo{}}
{
    mCommandBufferPools.reserve(threadCount);
    std::generate_n(std::back_inserter(mCommandBufferPools),
                    threadCount,
                    [this]()
                    {
                        return mDevice->createCommandBufferPool();
                    });
}

FrameObjects::~FrameObjects()
{
    if (!mCommandBufferPools.empty())
    {
        resetPools();
    }
}

const vk::raii::Semaphore&
FrameObjects::getRenderFinishedSemaphore() const
{
    return mRenderFinishedSemaphore;
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
