#pragma once

#include <cstdint>

namespace VOG::Graphics::Vulkan::Limits
{
constexpr std::uint8_t gMaxNumAttachments        = 3u;
constexpr std::uint8_t gMaxNumColorAttachments   = gMaxNumAttachments - 1u;
constexpr std::uint8_t gMaxNumDynamicStates      = 8u;
constexpr std::uint8_t gMaxNumPushConstants      = 8u;
constexpr std::uint8_t gMaxNumUniformBuffers     = 8u;
constexpr std::uint8_t gMaxNumVertexBuffers      = 4u;
constexpr std::uint8_t gMaxNumStageAttributes    = 8u;
constexpr std::uint8_t gMaxNumDescriptorSets     = 4u;
constexpr std::uint8_t gMaxNumDescriptorBindings = 4u;
} // namespace VOG::Graphics::Vulkan::Limits
