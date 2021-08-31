#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>

namespace VOG::Graphics::Vulkan
{
std::uint32_t inline FindMemoryType(vk::PhysicalDeviceMemoryProperties const& memoryProperties,
                                    std::uint32_t                             typeBits,
                                    vk::MemoryPropertyFlags                   requirementsMask)
{
    std::uint32_t typeIndex = uint32_t(~0);
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((typeBits & 1) && ((memoryProperties.memoryTypes[i].propertyFlags & requirementsMask) ==
                               requirementsMask))
        {
            typeIndex = i;
            break;
        }
        typeBits >>= 1;
    }
    assert(typeIndex != std::uint32_t(~0));
    return typeIndex;
}
} // namespace VOG::Graphics::Vulkan