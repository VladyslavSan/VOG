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

TimelineSemaphore::TimelineSemaphore(DevicePtr device)
    : DeviceChild{std::move(device)}
    , vk::raii::Semaphore{DeviceChild::device()->raii(), gCreateInfo}
{
}

vk::Result
TimelineSemaphore::waitOnCPU(std::uint64_t value, const std::uint64_t timeout) const
{
    return device()->waitSemaphores(
        {.semaphoreCount = 1, .pSemaphores = &(**this), .pValues = &value}, timeout);
}
} // namespace VOG::Graphics::Vulkan
