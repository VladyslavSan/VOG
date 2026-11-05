#pragma once

#include <VOG/Engine/Renderer/Shader.hpp>
#include <VOG/Engine/Renderer/Texture.hpp>
#include <VOG/Math/Math.hpp>

#include <compare>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace VOG::Engine
{
/**
 * The value of one parameter. Alternative i is the C++ type of Detail::ParameterType i (eFloat,
 * eFloat2, eFloat3, eFloat4, eInt, eInt2, eInt3, eInt4, eUInt, eUInt2, eUInt3, eUInt4, eFloat3x3,
 * eFloat4x4, eTexture). A literal picks its alternative by exact type: 1 is an int, 1u a uint,
 * 1.0f a float; 1.0 (a double) does not compile.
 */
using MaterialValue = std::variant<float,
                                   Math::Vector2f,
                                   Math::Vector3f,
                                   Math::Vector4f,
                                   std::int32_t,
                                   Math::Vector2i,
                                   Math::Vector3i,
                                   Math::Vector4i,
                                   std::uint32_t,
                                   Math::Vector2u,
                                   Math::Vector3u,
                                   Math::Vector4u,
                                   Math::Matrix3x3f,
                                   Math::Matrix4x4f,
                                   Texture>;

// A double does not convert to the float, because a conversion that narrows is not considered: the
// literal 1.0 does not compile, 1.0f does.
static_assert(std::is_constructible_v<MaterialValue, float>);
static_assert(!std::is_constructible_v<MaterialValue, double>);

struct MaterialParameter
{
    std::string   name;
    MaterialValue value;
};

enum class BlendMode : std::uint8_t
{
    eOpaque,
    eAlpha,
    eAdditive
};

enum class DepthMode : std::uint8_t
{
    eTestAndWrite,
    eTestOnly
};

/**
 * eBack discards faces that are counter-clockwise on screen. The world is left-handed, and front
 * is clockwise seen from the camera. The default discards nothing, so a sprite mirrored with
 * scale.x < 0 still draws.
 */
enum class CullMode : std::uint8_t
{
    eNone,
    eBack
};

/** The part of the pipeline a material chooses. */
struct RenderState
{
    BlendMode blend = BlendMode::eOpaque;
    DepthMode depth = DepthMode::eTestAndWrite;
    CullMode  cull  = CullMode::eNone;

    auto operator<=>(const RenderState&) const = default;
};

/**
 * A shader and the values of the parameters of its Material block. The shader reads them through
 * the descriptor heap: a Material block `VOG_MATERIAL { ... } gMaterials[];` of the include
 * `VOG/Draw.glsl`, indexed by the heap index that the renderer pushes with every draw.
 */
struct MaterialDescription
{
    /** Names the material in the errors of Renderer::createMaterial(); empty is allowed. */
    std::string name;
    /** From Renderer::getShader(). */
    Shader shader;
    /** Exactly one value per member of the shader's Material block, in any order. */
    std::vector<MaterialParameter> parameters;
    /** Stored with the material; the renderer does not apply it yet. */
    RenderState state;
};

namespace Detail
{
class MaterialData;
} // namespace Detail

class Renderer;

/**
 * Immutable after creation; made by Renderer::createMaterial(). A copy shares the material with
 * the original. A parameter that changes is a new material. A default-constructed Material is
 * empty: it is no material, and a draw of it is skipped.
 */
class Material
{
public:
    Material() = default;

    /** True for a material that createMaterial() made. */
    explicit
    operator bool() const noexcept
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Material&) const = default;

    /** The values it was created with, in the order given; empty for an empty Material. */
    [[nodiscard]] std::span<const MaterialParameter> parameters() const noexcept;

private:
    friend class Renderer;
    friend class ResourceFactory;
    friend class RenderQueue;
    friend class FrameLoop;
    friend struct DrawRecording;
    friend struct HandleTestAccess;

    explicit Material(std::shared_ptr<const Detail::MaterialData> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::MaterialData> mData;
};
} // namespace VOG::Engine
