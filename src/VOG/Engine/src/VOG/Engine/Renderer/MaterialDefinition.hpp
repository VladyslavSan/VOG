#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(HeapBuffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/** The most a Material block can take: the spec minimum of maxUniformBufferRange. */
constexpr std::size_t gMaxMaterialBlockSize = 16384u;

namespace Detail
{
class ShaderInfo;

class MaterialData
{
public:
    std::shared_ptr<const ShaderInfo> shader;

    /** The parameters, written once when the material was made; its heap index is pushed as
     *  `material`. */
    Graphics::Vulkan::HeapBufferPtr block;

    /** What the parameters hold: the textures that the block refers to by heap index. */
    std::vector<Texture> textures;

    /** As given to Renderer::createMaterial(). */
    std::vector<MaterialParameter> parameters;

    /** Stored; the renderer does not apply it yet. */
    RenderState state;
};
} // namespace Detail
} // namespace VOG::Engine
