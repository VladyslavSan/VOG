#include "VOG/Graphics/VulkanWrappers/CommandManager.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::VulkanWrappers
{
CommandSubmission::CommandSubmission(std::weak_ptr<CommandManager> commandManager,
                                     CommandBuffer commandBuffer, vk::raii::Fence fence,
                                     vk::raii::Semaphore semaphore)
    : mManager{std::move(commandManager)}
    , mCommandBuffer{std::move(commandBuffer)}
    , mExecutionFinishedFence(std::move(fence))
    , mExecutionFinishedSemaphore(std::move(semaphore))
{
#if !defined(VOG_DISABLE_SIMPLE_CHECKS)
    if (!*mExecutionFinishedFence)
        throw std::invalid_argument{"CommandSubmission invalid fence during construction"};

    if (!*mExecutionFinishedFence)
        throw std::invalid_argument{"CommandSubmission invalid fence during construction"};
#endif
}

CommandSubmission::~CommandSubmission()
{
    auto manager = mManager.lock();
    if (manager)
    {
        manager->ReturnToThePool(*this);
    }
}

const vk::raii::Fence&
CommandSubmission::GetFence() const
{
    return mExecutionFinishedFence;
}

const vk::raii::Semaphore&
CommandSubmission::GetSemaphore() const
{
    return mExecutionFinishedSemaphore;
}

CommandSubmission::WaitResult
CommandSubmission::Wait(std::uint64_t timeout)
{
    auto manager = mManager.lock();
    if (manager)
    {
        vk::Result waitResult = manager->GetGraphicsProvider()->GetDevice().waitForFences(
            {*mExecutionFinishedFence}, VK_TRUE, timeout);
        switch (waitResult)
        {
        case vk::Result::eTimeout:
            return WaitResult::Timeout;
        case vk::Result::eSuccess:
            return WaitResult::Finished;
        }
    }

    return WaitResult::WaitError;
}

CommandSubmission::WaitResult
CommandSubmission::WaitUntilFinished()
{
    while (true)
    {
        const WaitResult result = Wait(std::numeric_limits<std::uint64_t>::max());
        if (result != WaitResult::Timeout)
            return result;
    }
}

CommandSubmission::operator ExecutionDependency() const
{
    return ExecutionDependency{.semaphore = mExecutionFinishedSemaphore};
}

void
CommandManager::ReturnToThePool(CommandSubmission& submission)
{
    if (*submission.mExecutionFinishedFence && mPooledFences.size() < mPooledFences.capacity())
    {
        mGraphicsProvider->GetDevice().resetFences({*submission.mExecutionFinishedFence});
        mPooledFences.emplace_back(std::move(submission.mExecutionFinishedFence));
    }
    if (*submission.mExecutionFinishedSemaphore &&
        mPooledSemaphores.size() < mPooledSemaphores.capacity())
    {
        mPooledSemaphores.emplace_back(std::move(submission.mExecutionFinishedSemaphore));
    }
}

std::pair<vk::raii::Fence, vk::raii::Semaphore>
CommandManager::RequestSyncPrimitives()
{
    auto& device = mGraphicsProvider->GetDevice();
    auto result = std::make_pair(
        mPooledFences.empty() ? vk::raii::Fence{device, vk::FenceCreateInfo{}}
                              : std::move(mPooledFences.back()),
        mPooledSemaphores.empty() ? vk::raii::Semaphore{device, vk::SemaphoreCreateInfo{}}
                                  : std::move(mPooledSemaphores.back()));

    if (!mPooledFences.empty())
    {
        mPooledFences.pop_back();
    }
    if (!mPooledSemaphores.empty())
    {
        mPooledSemaphores.pop_back();
    }

    return result;
}

CommandManager::CommandManager(const Api::GraphicsProviderPtr& graphicsProvider,
                               std::size_t framesInFlight, std::size_t poolSize)
    : mGraphicsProvider(graphicsProvider)
{
    mPooledFences.reserve(poolSize);
    mPooledSemaphores.reserve(poolSize);

    mCommandPools.reserve(framesInFlight);
    for (std::size_t i = 0; i < framesInFlight; ++i)
    {
        mCommandPools.emplace_back(mGraphicsProvider->GetDevice(), vk::CommandPoolCreateInfo{});
    }
}

CommandManager::RequestProxy
CommandManager::Request(std::size_t requesterId)
{
    return RequestProxy{*this, mGraphicsProvider->GetDevice(),
                        mCommandPools[requesterId % mCommandPools.size()]};
}

CommandSubmission
CommandManager::RequestProxy::SubmitCommandBuffer(CommandBuffer commandBuffer,
                                                  std::optional<ExecutionDependency> dependency)
{
    auto [fence, semaphore] = mManager.RequestSyncPrimitives();

    vk::SubmitInfo submitInfo{};
    submitInfo.setSignalSemaphores(*semaphore);

    if (false)
    {
        vk::PipelineStageFlags waitDestinationStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        submitInfo.setWaitDstStageMask(waitDestinationStageMask);
        submitInfo.setWaitSemaphoreCount(1);
    }

    std::array<vk::CommandBuffer, 1> commandBuffers = {*commandBuffer};
    const_cast<vk::CommandBuffer&>(*commandBuffer) = nullptr;

    submitInfo.setCommandBuffers(commandBuffers);
    mManager.mGraphicsProvider->GetGraphicsQueue().submit(submitInfo, *fence);

    return {mManager.weak_from_this(), std::move(commandBuffer), std::move(fence),
            std::move(semaphore)};
}

void
CommandManager::RequestProxy::ResetPool()
{
    mCommandPool.reset();
}

CommandBuffer
CommandManager::RequestProxy::MakeCommandBuffer()
{
    vk::CommandBufferAllocateInfo commandBufferAllocateInfo(*mCommandPool,
                                                            vk::CommandBufferLevel::ePrimary, 1);
    vk::raii::CommandBuffers commandBuffers{mDevice, commandBufferAllocateInfo};

    return {std::move(commandBuffers.at(0))};
}
} // namespace VOG::Graphics::VulkanWrappers