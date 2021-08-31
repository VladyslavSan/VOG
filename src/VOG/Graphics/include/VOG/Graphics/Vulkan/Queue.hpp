#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics::Vulkan
{
class Queue : public vk::raii::Queue
{
public:
    Queue(vk::raii::Queue           queue,
          std::uint32_t             _familyIndex,
          vk::QueueFamilyProperties _familyProperties)
        : vk::raii::Queue{std::move(queue)}
        , familyIndex{_familyIndex}
        , familyProperties{_familyProperties}
    {
    }

    const std::uint32_t             familyIndex;
    const vk::QueueFamilyProperties familyProperties;
};
} // namespace VOG::Graphics::Vulkan
