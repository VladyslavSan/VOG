#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/ThreadObjects.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <span>
#include <vector>

namespace VOG::Graphics
{
class GraphicsProvider;
}
namespace VOG::Graphics::Frame
{
class FrameObjects
{
public:
    FrameObjects(const GraphicsProvider& graphicsProvider, std::size_t threadCount);

    const vk::raii::Semaphore& getFramePresentWaitSemaphore() const;

    Vulkan::TimelineSemaphore&       getTimelineSemaphore();
    const Vulkan::TimelineSemaphore& getTimelineSemaphore() const;

    /**
     * Get the thread objects by id.
     *
     * @param threadId
     *
     * @note for a thread pool thread ids should be unique as access to thread objects is not
     * guarded.
     */
    ThreadObjects& getThreadObjects(std::size_t threadId);

    /**
     * Frame cleanups from previous usage like resetting the command buffers and command pools etc.
     */
    void onFrameStart();

    void submit(Vulkan::CommandBufferHandle              handle,
                Vulkan::TimelineSemaphore::WaitRequest   wait,
                Vulkan::TimelineSemaphore::SignalRequest signal,
                const vk::raii::Semaphore&               signalBinary);

protected:
    /** Graphics provider */
    const GraphicsProvider& mGraphicsProvider;

    /**
     * Semaphore that will be signaled by last command buffer of the frame and waited on before
     * presenting frame to surface.
     * @note this semaphore should be used carefully as there is no way to reset non timeline
     * semaphore state.
     */
    vk::raii::Semaphore mFramePresentWaitSemaphore;

    /** Frame's timeline semaphore used for chaining the command buffers execution */
    Vulkan::TimelineSemaphore mTimelineSemaphore;

    /** Per working thread objects */
    std::vector<ThreadObjects> mThreadObjects;

    /** A list of used command buffer handles during frame recording */
    std::vector<Vulkan::CommandBufferHandle> mUsedCommandBuffers;
};
} // namespace VOG::Graphics::Frame
