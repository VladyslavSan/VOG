#include "VOG/Graphics/Vulkan/TimelineSemaphore.hpp"

#include "VOG/Graphics/GraphicsProvider.hpp"

namespace VOG::Graphics::Vulkan
{
namespace
{
inline constexpr vk::SemaphoreTypeCreateInfo kTypeCreateInfo{
    .semaphoreType = vk::SemaphoreType::eTimeline, .initialValue = 0};
inline constexpr vk::SemaphoreCreateInfo kCreateInfo{.pNext = &kTypeCreateInfo};
} // namespace

TimelineSemaphore::TimelineSemaphore(const Vulkan::Device& device)
    : vk::raii::Semaphore{device, kCreateInfo}
    , mDevice{device}
{
}

vk::Result
TimelineSemaphore::waitOnCPU(std::uint64_t value, std::uint64_t timeout)
{
    return mDevice.waitSemaphores(
        {.semaphoreCount = 1, .pSemaphores = &(**this), .pValues = &value}, timeout);
}

std::uint64_t
TimelineSemaphore::getCurrentWaitValue()
{
    return mCurrentValue;
}

std::uint64_t
TimelineSemaphore::getNextWaitValue()
{
    return ++mCurrentValue;
}
} // namespace VOG::Graphics::Vulkan
