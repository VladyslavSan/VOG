#pragma once

#include <VOG/Engine/Renderer/Shader.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(ShaderProgram);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
namespace Detail
{
/**
 * The type of a parameter of a Material block. The order is the order of the alternatives of
 * MaterialValue: the index of a value is its type.
 */
enum class ParameterType : std::uint8_t
{
    eFloat,
    eFloat2,
    eFloat3,
    eFloat4,
    eInt,
    eInt2,
    eInt3,
    eInt4,
    eUInt,
    eUInt2,
    eUInt3,
    eUInt4,
    eFloat3x3,
    eFloat4x4,
    eTexture
};

/** How GLSL names the type: "vec4", "uvec2", "mat3", "TextureRef". */
std::string_view describe(ParameterType type);

/** A member of the Material block of a shader, where the shader reads it. */
struct MaterialMember
{
    std::string   name;
    ParameterType type;
    std::uint32_t offset;
    /** Bytes between the columns of a matrix; 0 for the other types. */
    std::uint32_t matrixStride;
};

/**
 * A program of the shader directory, and what the renderer needs to fill the Material block that
 * its stages read. Made by ShaderLibrary.
 */
class ShaderInfo
{
public:
    std::string                        name;
    Graphics::Vulkan::ShaderProgramPtr program;

    /** The members of the Material block in block order; empty if no stage reads one. */
    std::vector<MaterialMember> members;

    /** The same members for the public handle; the order is the same. */
    std::vector<ShaderParameter> parameters;

    /** Bytes of the Material block as declared; 0 if no stage reads one. */
    std::uint32_t blockSize = 0u;
};
} // namespace Detail
} // namespace VOG::Engine
