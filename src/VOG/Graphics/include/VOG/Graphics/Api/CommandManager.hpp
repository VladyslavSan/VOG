#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <vector>

namespace vk::raii
{
class CommandBuffer;
class CommandPool;
class Fence;
class Semaphore;
} // namespace vk::raii

namespace VOG::Graphics::Api
{
class CommandManager;
VOG_DECLARE_PTR(GraphicsProvider);

struct ExecutionDependency
{
    vk::raii::Semaphore& semaphore;
};
/**
 * Implements a submission entry in CommandBufferManager class.
 * Desired use case:
 * 1) Submit command buffers to CommandBufferManager and get an instance of this class
 * 2) Later wait for the commands execution finish and return
 */
class CommandsSubmission
{
    friend class CommandManager;

    CommandsSubmission(std::unique_ptr<vk::raii::Fence> fence,
                       std::unique_ptr<vk::raii::Semaphore> semaphore);

public:
    ~CommandsSubmission();
    /** Enum with possible outcome cases for wait operation */
    enum class WaitResult
    {
        NotStarted,
        Timeout,
        Finished
    };

    /** Getter for the submission fence */
    const vk::raii::Fence& GetFence() const;

    /** Getter for the submission semaphore */
    const vk::raii::Semaphore& GetSemaphore() const;

    /** Blocks calling thread until finished or timeout occurs*/
    WaitResult Wait(std::uint64_t timeout);

    /** Blocks calling thread until finished */
    WaitResult WaitUntilFinished();

    operator ExecutionDependency() const;

protected:
    /** Reference to its' manager */
    std::weak_ptr<CommandManager> m_manager;

    /** Semaphore that can be used for chain dependency between submissions */
    std::unique_ptr<vk::raii::Fence> m_executionFinishedFence;

    /** Fence that can be used submission execution checks and waits */
    std::unique_ptr<vk::raii::Semaphore> m_executionFinishedSemaphore;
};

/**
 * Used for command buffer allocation and submission
 * For single thread usage only.
 * Todo: multithreading?
 *
 */
class CommandManager
{
    friend class CommandsSubmission;

    void ReturnToThePool(CommandsSubmission& submission);

    const GraphicsProviderPtr& GetGraphicsProvider() const;

public:
    CommandManager(const GraphicsProviderPtr& graphicsProvider, std::size_t framesInFlight,
                   std::size_t poolSize = 32);

    std::unique_ptr<vk::raii::CommandBuffer> MakeCommandBuffer(std::size_t requesterId);

    /**
     * Submit command buffers and get submission object for further signaling
     */
    std::unique_ptr<CommandsSubmission>
    SubmitCommandBuffers(std::size_t requesterId,
                         std::vector<std::unique_ptr<vk::raii::CommandBuffer>> commandBuffers);

    void ResetCommands(std::size_t requesterId);

protected:
    GraphicsProviderPtr m_graphicsProvider;

    std::vector<vk::raii::CommandPool> m_commandPools;
    std::vector<std::unique_ptr<vk::raii::Fence>> m_pooledFences;
    std::vector<std::unique_ptr<vk::raii::Semaphore>> m_pooledSemaphores;
};

inline const GraphicsProviderPtr&
CommandManager::GetGraphicsProvider() const
{
    return m_graphicsProvider;
}
} // namespace VOG::Graphics::Api