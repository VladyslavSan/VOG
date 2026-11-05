#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
namespace Detail
{
class ShaderInfo;
} // namespace Detail

/**
 * Every shader of a directory, compiled, linked and checked against what the renderer pushes and
 * fills, when the library is made. A shader is a pair of files, `Name.vert.glsl` and
 * `Name.frag.glsl`; the includes of both are looked up in the directory `include` of the shader
 * directory, as `#include "VOG/Draw.glsl"`.
 *
 * What the renderer needs of a shader, each a failure at load that names the shader and its files:
 * - the members of the push block of a program are members of DrawPushData, at its offsets;
 * - a heap block named `Frame` is the FrameBlock;
 * - a heap block named `Material` is a uniform block of at most 16384 bytes whose members are
 *   scalars, vectors and the matrices mat3 and mat4 of 32-bit int, uint and float, and the struct
 *   TextureRef { uint index; uint samplerIndex; }: no array, no row_major matrix, no other struct;
 * - no other heap block.
 * A heap block that no stage reads is not in the SPIR-V: a shader that declares `Material` and
 * never reads it has no Material block, so no parameters.
 */
class ShaderLibrary
{
public:
    /**
     * @throws std::runtime_error if @p directory does not exist, a shader has only one of its two
     *         files, or a shader fails to load; the exception of a shader that fails holds the
     *         reason as a nested exception.
     */
    ShaderLibrary(const Graphics::Vulkan::DevicePtr& device,
                  const std::filesystem::path&       directory);

    /** @throws std::invalid_argument if the directory has no shader @p name. */
    [[nodiscard]] std::shared_ptr<const Detail::ShaderInfo> get(std::string_view name) const;

private:
    std::filesystem::path mDirectory;

    std::map<std::string, std::shared_ptr<const Detail::ShaderInfo>, std::less<>> mShaders;
};
} // namespace VOG::Engine
