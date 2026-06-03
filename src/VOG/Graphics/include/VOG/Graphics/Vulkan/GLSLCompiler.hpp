#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <string>
#include <vector>

namespace VOG::Graphics::Vulkan
{
std::vector<std::uint32_t> compileGLSL(Shader::ShadingStage stage, const std::string& glslCode);
} // namespace VOG::Graphics::Vulkan
