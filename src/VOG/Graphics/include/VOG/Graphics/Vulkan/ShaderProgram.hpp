#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

namespace VOG::Graphics::Vulkan
{
struct ShadingStages
{
    ShaderPtr vertexFunction;
    ShaderPtr fragmentFunction;
};
} // namespace VOG::Graphics::Vulkan
