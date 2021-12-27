#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class Device;
class TimelineSemaphore : public vk::raii::Semaphore
{
public:
    struct WaitRequest
    {
        const TimelineSemaphore* semaphore = nullptr;
        const std::uint64_t      value     = 0u;
    };
    struct SignalRequest
    {
        const TimelineSemaphore* semapore = nullptr;
        const std::uint64_t      value    = 0u;
    };

    TimelineSemaphore(const Vulkan::Device& device);

    vk::Result waitOnCPU(std::uint64_t value, std::uint64_t timeout);

    std::uint64_t getCurrentWaitValue();

    std::uint64_t getNextWaitValue();

protected:
    const Vulkan::Device& mDevice;
    std::uint64_t         mCurrentValue = 0u;
};
} // namespace VOG::Graphics::Vulkan
