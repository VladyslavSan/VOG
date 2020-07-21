#include "VOG/Graphics/Api/CommandManager.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <stdexcept>

namespace VOG::Graphics::Api
{
CommandsSubmission::CommandsSubmission(std::unique_ptr<vk::raii::Fence> fence,
                                       std::unique_ptr<vk::raii::Semaphore> semaphore)
    : m_executionFinishedFence(std::move(fence))
    , m_executionFinishedSemaphore(std::move(semaphore))
{
#if !defined(VOG_DISABLE_SIMPLE_CHECKS)
    if (!m_executionFinishedFence)
        throw std::invalid_argument{"CommandsSubmission invalid fence during construction"};

    if (!m_executionFinishedFence)
        throw std::invalid_argument{"CommandsSubmission invalid fence during construction"};
#endif
}

CommandsSubmission::~CommandsSubmission()
{
    auto manager = m_manager.lock();
    if (manager)
    {
        manager->ReturnToThePool(*this);
    }
}

const vk::raii::Fence&
CommandsSubmission::GetFence() const
{
    return *m_executionFinishedFence;
}

const vk::raii::Semaphore&
CommandsSubmission::GetSemaphore() const
{
    return *m_executionFinishedSemaphore;
}

CommandsSubmission::WaitResult
CommandsSubmission::Wait(std::uint64_t timeout)
{
    auto manager = m_manager.lock();
    if (manager)
    {
        vk::Result waitResult = manager->GetGraphicsProvider()->GetDevice()->waitForFences(
            {**m_executionFinishedFence}, VK_TRUE, timeout);
        switch (waitResult)
        {
        case vk::Result::eTimeout:
            return WaitResult::Timeout;
        case vk::Result::eSuccess:
            return WaitResult::Finished;
        }
    }

    return WaitResult::NotStarted;
}

CommandsSubmission::WaitResult
CommandsSubmission::WaitUntilFinished()
{
    return Wait(std::numeric_limits<std::uint64_t>::max());
}

CommandsSubmission::operator ExecutionDependency() const
{
    return ExecutionDependency{.semaphore = *m_executionFinishedSemaphore};
}

void
CommandManager::ReturnToThePool(CommandsSubmission& submission)
{
    if (m_pooledFences.size() < m_pooledFences.capacity())
    {
        m_pooledFences.emplace_back(std::move(submission.m_executionFinishedFence));
        m_pooledSemaphores.emplace_back(std::move(submission.m_executionFinishedSemaphore));
    }
}

CommandManager::CommandManager(const GraphicsProviderPtr& graphicsProvider,
                               std::size_t framesInFlight, std::size_t poolSize)
    : m_graphicsProvider(graphicsProvider)
{
    m_pooledFences.reserve(poolSize);
    m_pooledSemaphores.reserve(poolSize);

    m_commandPools.reserve(framesInFlight);
    for (std::size_t i = 0; i < framesInFlight; ++i)
    {
        m_commandPools.emplace_back(*m_graphicsProvider->GetDevice(), vk::CommandPoolCreateInfo{});
    }
}

std::unique_ptr<vk::raii::CommandBuffer>
CommandManager::MakeCommandBuffer(std::size_t requesterId)
{
    auto& commandPool = m_commandPools[requesterId % m_commandPools.size()];
    vk::CommandBufferAllocateInfo commandBufferAllocateInfo(*commandPool,
                                                            vk::CommandBufferLevel::ePrimary, 1);
    return std::make_unique<vk::raii::CommandBuffer>(std::move(
        vk::raii::CommandBuffers{*m_graphicsProvider->GetDevice(), commandBufferAllocateInfo}.at(
            0)));
}

std::unique_ptr<CommandsSubmission>
CommandManager::SubmitCommandBuffers(
    std::size_t requesterId, std::vector<std::unique_ptr<vk::raii::CommandBuffer>> commandBuffers)
{
    // Unlikely path but handle it like that, should never be actually called without command
    // buffers Todo: throw exception?
    if (commandBuffers.empty())
        return nullptr;

    std::unique_ptr<vk::raii::Fence> fence;
    std::unique_ptr<vk::raii::Semaphore> semaphore;
    if (!m_pooledFences.empty())
    {
        fence = std::move(m_pooledFences.back());
        m_pooledFences.pop_back();

        semaphore = std::move(m_pooledSemaphores.back());
        m_pooledSemaphores.pop_back();
    }
    else
    {
        fence = std::make_unique<vk::raii::Fence>(*m_graphicsProvider->GetDevice(),
                                                  vk::FenceCreateInfo{});
        semaphore = std::make_unique<vk::raii::Semaphore>(*m_graphicsProvider->GetDevice(),
                                                          vk::SemaphoreCreateInfo{});
    }

    vk::SubmitInfo submitInfo{};
    submitInfo.setSignalSemaphores(**semaphore);
    // Prerequisite branch
    if (false)
    {
        vk::PipelineStageFlags waitDestinationStageMask(
            vk::PipelineStageFlagBits::eColorAttachmentOutput);
        submitInfo.setWaitDstStageMask(waitDestinationStageMask);
        submitInfo.setWaitSemaphoreCount(1);
    }

    std::vector<vk::CommandBuffer> commandBuffersVec;
    commandBuffersVec.reserve(commandBuffers.size());
    for (auto& buffer : commandBuffers)
    {
        commandBuffersVec.emplace_back(**buffer);
        // Detach the command buffer
        const_cast<vk::CommandBuffer&>(**buffer) = nullptr;
    }

    submitInfo.setCommandBuffers(commandBuffersVec);
    m_graphicsProvider->GetGraphicsQueue()->submit(submitInfo, **fence);

    return std::unique_ptr<CommandsSubmission>{
        new CommandsSubmission{std::move(fence), std::move(semaphore)}};
}

void
CommandManager::ResetCommands(std::size_t requesterId)
{
    auto& commandPool = m_commandPools[requesterId % m_commandPools.size()];
    commandPool.reset();
}
} // namespace VOG::Graphics::Api