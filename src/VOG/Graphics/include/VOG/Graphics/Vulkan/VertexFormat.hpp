#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics::Vulkan
{
enum class VertexAttribute : std::uint8_t
{
    ePosition = 0,
    eColor    = 1,
    eTexcoord = 2,
    eNormal   = 3
};
} // namespace VOG::Graphics::Vulkan
