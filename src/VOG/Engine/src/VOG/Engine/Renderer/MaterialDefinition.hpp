#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(ShaderProgram);
VOG_DECLARE_PTR(UniformBlock);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/** Spec minimum of maxUniformBufferRange. */
constexpr std::size_t gMaxParametersSize = 16384u;
constexpr std::size_t gMaxTextures       = 4u;

/**
 * The push data of every draw, at std430 offsets: `mat4 mvp` at 0, `uint material` (the heap index
 * of the parameter block) after it, then `uint textures[4]`.
 */
constexpr std::size_t gMaterialIndexOffset = sizeof(Math::Matrix4x4f);
constexpr std::size_t gTexturesOffset      = gMaterialIndexOffset + sizeof(std::uint32_t);
constexpr std::size_t gPushDataSize = gTexturesOffset + (gMaxTextures * sizeof(std::uint32_t));
/** Size of the push block in the shaders (`mat4 mvp; uint material; uint textures[4]`). */
constexpr std::size_t gShaderPushBlockSize = 84u;
static_assert(gPushDataSize == gShaderPushBlockSize);

class Material
{
public:
    Graphics::Vulkan::ShaderProgramPtr program;
    /** The parameters; its heap index is pushed as `material`. */
    Graphics::Vulkan::UniformBlockPtr block;
    std::vector<TexturePtr>           textures;
};
} // namespace VOG::Engine
