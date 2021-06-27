#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/VulkanWrappers/CommandBuffer.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace VOG::Graphics::Api
{
VOG_DECLARE_PTR(GraphicsProvider);
}

namespace VOG::Graphics::VulkanWrappers
{
class CommandManager;

struct ExecutionDependency
{
    const vk::raii::Semaphore& semaphore;
};
/**
 * Implements a submission entry in CommandBufferManager class.
 * Desired use case:
 * 1) Submit command buffers to CommandBufferManager and get an instance of this class
 * 2) Later wait for the commands execution finish and return
 */
class CommandSubmission
{
    friend class CommandManager;

    CommandSubmission(std::weak_ptr<CommandManager> commandManager, CommandBuffer commandBuffer,
                      vk::raii::Fence fence, vk::raii::Semaphore semaphore);

public:
    CommandSubmission(CommandSubmission&&) = default;
    ~CommandSubmission();

    /** Enum with possible outcome cases for wait operation */
    enum class WaitResult
    {
        WaitError,
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
    std::weak_ptr<CommandManager> mManager;

    /** Command buffer to hold unti it finishes */
    CommandBuffer mCommandBuffer;

    /** Semaphore that can be used for chain dependency between submissions */
    vk::raii::Fence mExecutionFinishedFence;

    /** Fence that can be used submission execution checks and waits */
    vk::raii::Semaphore mExecutionFinishedSemaphore;
};

/**
 * Used for command buffer allocation and submission
 * For single thread usage only.
 * Todo: multithreading?
 *
 */
class CommandManager : public std::enable_shared_from_this<CommandManager>
{
    friend class CommandSubmission;

    void ReturnToThePool(CommandSubmission& submission);

    const Api::GraphicsProviderPtr& GetGraphicsProvider() const;

    std::pair<vk::raii::Fence, vk::raii::Semaphore> RequestSyncPrimitives();

public:
    class RequestProxy
    {
    public:
        RequestProxy(CommandManager& manager, vk::raii::Device& device,
                     vk::raii::CommandPool& commandPool)
            : mManager{manager}
            , mDevice{device}
            , mCommandPool{commandPool}
        {
        }

        RequestProxy& operator=(RequestProxy&&) = delete;
        RequestProxy(RequestProxy&&) = delete;

        CommandBuffer MakeCommandBuffer();

        /**
         * Submit command buffers and get submission object for further signaling
         */
        CommandSubmission SubmitCommandBuffer(CommandBuffer commandBuffer,
                                              std::optional<ExecutionDependency> dependency = {});

        void ResetPool();

    private:
        CommandManager& mManager;
        vk::raii::Device& mDevice;
        vk::raii::CommandPool& mCommandPool;
    };

    CommandManager(const Api::GraphicsProviderPtr& graphicsProvider, std::size_t framesInFlight,
                   std::size_t poolSize = 32);

    RequestProxy Request(std::size_t id);

protected:
    friend class Request;

    Api::GraphicsProviderPtr mGraphicsProvider;

    std::vector<vk::raii::CommandPool> mCommandPools;
    std::vector<vk::raii::Fence> mPooledFences;
    std::vector<vk::raii::Semaphore> mPooledSemaphores;
};

inline const Api::GraphicsProviderPtr&
CommandManager::GetGraphicsProvider() const
{
    return mGraphicsProvider;
}
} // namespace VOG::Graphics::VulkanWrappers