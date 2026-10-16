#pragma once

#include <VOG/Engine/Renderer/Texture.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace VOG::Engine
{
/**
 * A shader, the parameters it draws with and the textures it samples.
 *
 * The shader reads them through the descriptor heap, by indices that the renderer pushes with
 * every draw: the push data is `mat4 mvp; uint material; uint textures[4]`, and the parameters
 * are the uniform block `gMaterials[pushData.material]`. The renderer cannot see how much of that
 * block the shader reads. A shader whose block is larger than the parameters (but at least 16
 * bytes, the smallest size of a block) reads past the end of it, which is undefined behavior: the
 * device has no `robustBufferAccess`.
 */
struct MaterialDescription
{
    /** Name of a shader in the shader directory's config.json. */
    std::string shader;
    /** At most 16384 bytes. Copied into an immutable uniform block. */
    std::vector<std::byte> parameters;
    /**
     * At most 4, none null. The shader reads texture i as `pushData.textures[i]`. A shader that
     * reads `pushData.textures[i]` for i at or past the size of this vector gets index 0, which
     * is an arbitrary heap slot: undefined behavior that the renderer cannot detect, like reading
     * past the parameter block.
     */
    std::vector<TexturePtr> textures;
};

/** Immutable after creation; made by Renderer::createMaterial(). */
class Material;
using MaterialPtr = std::shared_ptr<const Material>;
} // namespace VOG::Engine
