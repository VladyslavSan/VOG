#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace VOG::Engine
{
/** A shader and the parameters it draws with. */
struct MaterialDescription
{
    /** Name of a shader in the shader directory's config.json. */
    std::string shader;
    /** At most 64 bytes. The shader reads them as push constants from offset 64. */
    std::vector<std::byte> parameters;
};

/** Immutable after creation; made by Renderer::createMaterial(). */
class Material;
using MaterialPtr = std::shared_ptr<const Material>;
} // namespace VOG::Engine
