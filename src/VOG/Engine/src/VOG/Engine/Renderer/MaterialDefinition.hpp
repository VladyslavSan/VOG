#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Math/Math.hpp>

#include <cstddef>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(ShaderProgram);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/** The renderer pushes the model-view-projection matrix at offset 0 and the parameters after it. */
constexpr std::size_t gParametersOffset  = sizeof(Math::Matrix4x4f);
constexpr std::size_t gMaxParametersSize = 64u;

class Material
{
public:
    Graphics::Vulkan::ShaderProgramPtr program;
    std::vector<std::byte>             parameters;
};
} // namespace VOG::Engine
