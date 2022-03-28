#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

namespace VOG::Graphics::Vulkan
{
class ShaderProgram
{
public:
    ShaderPtr vertexFunction   = nullptr;
    ShaderPtr fragmentFunction = nullptr;
};
} // namespace VOG::Graphics::Vulkan
