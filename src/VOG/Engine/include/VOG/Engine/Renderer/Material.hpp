#pragma once

#include <VOG/Engine/Renderer/Texture.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
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
     * At most 4, none empty. The shader reads texture i as `pushData.textures[i]`. A shader that
     * reads `pushData.textures[i]` for i at or past the size of this vector gets index 0, which
     * is an arbitrary heap slot: undefined behavior that the renderer cannot detect, like reading
     * past the parameter block.
     */
    std::vector<Texture> textures;
};

namespace Detail
{
class MaterialData;
} // namespace Detail

class Renderer;

/**
 * Immutable after creation; made by Renderer::createMaterial(). A copy shares the material with
 * the original. A default-constructed Material is empty: it is no material, and a draw of it is
 * skipped.
 */
class Material
{
public:
    Material() = default;

    /** True for a material that createMaterial() made. */
    explicit
    operator bool() const
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Material&) const = default;

private:
    friend class Renderer;
    friend struct HandleTestAccess;

    explicit Material(std::shared_ptr<const Detail::MaterialData> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::MaterialData> mData;
};
} // namespace VOG::Engine
