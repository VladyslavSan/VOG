#pragma once

#include <cstdint>

namespace VOG::Graphics::Vulkan::Limits
{
constexpr std::uint8_t gMaxNumVertexBuffers    = 4u;
constexpr std::uint8_t gMaxNumVertexAttributes = 8u;
constexpr std::uint8_t gMaxNumPushConstants    = 8u;
constexpr std::uint8_t gMaxNumUniformBuffers   = 8u;
} // namespace VOG::Graphics::Vulkan::Limits