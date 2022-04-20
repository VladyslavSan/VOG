#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class Device;

/**
 * Timeline semaphore's main perk is that it allows very easy mechanism for chaining synchronization
 * using the counter.
 */
class TimelineSemaphore : public vk::raii::Semaphore
{
public:
    /** Describes wait operation on timeline semaphore. */
    struct WaitRequest
    {
        const TimelineSemaphore* semaphore = nullptr;
        const std::uint64_t      value     = 0u;
    };

    /** Describes signal operation on timeline semaphore. */
    struct SignalRequest
    {
        const TimelineSemaphore* semaphore = nullptr;
        const std::uint64_t      value     = 0u;
    };

    TimelineSemaphore(const Vulkan::Device& device);

    [[nodiscard("Wait result can end with success, fail and timeout.")]] vk::Result
    waitOnCPU(std::uint64_t value, std::uint64_t timeout);

    /**
     * Get counter value that might be used for waitOnCPU() or wait/signal value.
     *
     * @return counter value.
     */
    std::uint64_t getCounter() const;

    /**
     * Increment counter value and return it. See getCounter() for additional info.
     */
    void incrementCounter();

    /**
     * Conversion op to use semaphore as a wait request with counter value for wait op.
     */
    operator WaitRequest() const;

    /**
     * Conversion op to use semaphore as a signal request with counter value for signal op.
     */
    operator SignalRequest() const;

protected:
    const Vulkan::Device& mDevice;
    std::uint64_t         mCurrentValue = 0u;
};
} // namespace VOG::Graphics::Vulkan
