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
} // namespace VOG::Graphics::Vulkan
