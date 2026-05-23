#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
const std::uint32_t gGrowSize = 10u;
} // namespace

CommandBufferHandle::CommandBufferHandle(CommandBufferPoolPtr pool,
                                         CommandBuffer        commandBuffer) noexcept
    : mPool{std::move(pool)}
    , mCommandBuffer{std::move(commandBuffer)}
{
}

CommandBufferHandle::~CommandBufferHandle()
{
    if (!mPool || !*mCommandBuffer)
    {
        return;
    }

    mPool->returnCommandBufferToPool(std::move(mCommandBuffer));
}

vk::CommandBuffer
CommandBufferHandle::consumeForSubmission(std::shared_ptr<FencePool::FenceHandle> fence)
{
    VOG_ASSERT_MSG(fence, "Fence must be provided for command buffer submission.");
    vk::CommandBuffer unmanagedHandle = *mCommandBuffer;

    mPool->mSubmittedCommandBuffers.push_back(CommandBufferPool::SubmittedCommandBuffer{
        .commandBuffer = std::move(mCommandBuffer), .fence = std::move(fence)});

    return unmanagedHandle;
}

CommandBuffer*
CommandBufferHandle::operator*() noexcept
{
    return std::addressof(mCommandBuffer);
}

CommandBuffer*
CommandBufferHandle::operator->() noexcept
{
    return std::addressof(mCommandBuffer);
}

CommandBufferPool::CommandBufferPool(const DevicePtr& device)
    : mDevice{device}
    , mVulkanPool{*mDevice,
                  vk::CommandPoolCreateInfo{.queueFamilyIndex =
                                                mDevice->queueInfos.graphics.familyIndex}}
{
}

CommandBufferHandle
CommandBufferPool::get(vk::CommandBufferLevel type)
{
    auto& pool = type == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers : mSecondaryBuffers;
    if (pool.empty())
    {
        auto newBuffers = Vulkan::CommandBuffer::create(*mDevice, mVulkanPool, type, gGrowSize);
        for (auto& buffer : newBuffers)
        {
            pool.push_back(std::move(buffer));
        }
    }

    Vulkan::CommandBuffer result = std::move(pool.back());
    pool.pop_back();

    return {shared_from_this(), std::move(result)};
}

void
CommandBufferPool::returnCommandBufferToPool(Vulkan::CommandBuffer commandBuffer)
{
    commandBuffer.reset();
    auto& pool = commandBuffer.type == vk::CommandBufferLevel::ePrimary ? mPrimaryBuffers
                                                                        : mSecondaryBuffers;

    pool.push_back(std::move(commandBuffer));
}

void
CommandBufferPool::reset()
{
    for (auto& submission : mSubmittedCommandBuffers)
    {
        const auto waitResult = submission.fence->wait(std::numeric_limits<std::uint64_t>::max());
        VOG_ASSERT(waitResult == vk::Result::eSuccess);

        returnCommandBufferToPool(std::move(submission.commandBuffer));
    }
    mSubmittedCommandBuffers.clear();

    mVulkanPool.reset();
}
} // namespace VOG::Graphics::Vulkan
