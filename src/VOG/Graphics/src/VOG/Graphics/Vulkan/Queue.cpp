#include "VOG/Graphics/Vulkan/Queue.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
Queue::Queue(const Device&             device,
             std::uint32_t             _familyIndex,
             vk::QueueFamilyProperties _familyProperties)
    : vk::raii::Queue{device.raii(), _familyIndex, 0u}
    , familyIndex{_familyIndex}
    , familyProperties{_familyProperties}
{
}
} // namespace VOG::Graphics::Vulkan
