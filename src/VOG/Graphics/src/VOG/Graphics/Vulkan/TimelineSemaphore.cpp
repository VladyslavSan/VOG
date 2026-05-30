#include "VOG/Graphics/Vulkan/TimelineSemaphore.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
inline constexpr vk::SemaphoreTypeCreateInfo gTypeCreateInfo{
    .semaphoreType = vk::SemaphoreType::eTimeline, .initialValue = 0};
inline constexpr vk::SemaphoreCreateInfo gCreateInfo{.pNext = &gTypeCreateInfo};
} // namespace

TimelineSemaphore::TimelineSemaphore(const Vulkan::DevicePtr& device)
    : vk::raii::Semaphore{*device, gCreateInfo}
    , mDevice{device}
{
}

vk::Result
TimelineSemaphore::waitOnCPU(std::uint64_t value, std::uint64_t timeout)
{
    return mDevice->waitSemaphores(
        {.semaphoreCount = 1, .pSemaphores = &(**this), .pValues = &value}, timeout);
}

std::uint64_t
TimelineSemaphore::getCounter() const
{
    return mCurrentValue;
}

void
TimelineSemaphore::incrementCounter()
{
    ++mCurrentValue;
}
TimelineSemaphore::
operator WaitRequest() const
{
    return {.semaphore = this, .value = mCurrentValue};
}

TimelineSemaphore::
operator SignalRequest() const
{
    return {.semaphore = this, .value = mCurrentValue};
}
} // namespace VOG::Graphics::Vulkan
