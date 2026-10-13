#include "VOG/Graphics/Vulkan/Semaphore.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
Semaphore::Semaphore(DevicePtr device)
    : DeviceChild{std::move(device)}
    , vk::raii::Semaphore{DeviceChild::device()->raii(), vk::SemaphoreCreateInfo{}}
{
}
} // namespace VOG::Graphics::Vulkan
