#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace VOG::Engine
{
struct VertexLayout;

namespace Detail
{
class MeshData;
class ShaderInfo;

/** The first vertex input of a shader that a vertex layout cannot feed. */
struct MissingInput
{
    enum class Kind : std::uint8_t
    {
        /** The layout has no attribute at the location. */
        eNoAttribute,
        /** The attribute is there, and its numeric type is not the one of the input. */
        eWrongType
    };

    Kind          kind;
    std::uint32_t location;
    /** How the shader declares the type of the input: "vec3". */
    std::string type;
    /** The name of the input in the shader. */
    std::string name;
};

/**
 * Decides whether a mesh can be drawn with a shader: the mesh provides, at every location a vertex
 * input of the shader reads, an attribute of a compatible numeric type. Fewer components than the
 * input declares are allowed; Vulkan fills the missing ones.
 *
 * The decision depends on the vertex layout and the shader program only, so require() keeps it for
 * each pair of layoutId and programId: a failure too, so a caller that catches the DrawError and
 * goes on gets it again for the next draw with that pair, under the name of that draw.
 *
 * Main thread only: it takes no lock, because Renderer::submitFrame() runs one thread at a time.
 */
class DrawCheck
{
public:
    /** Pure, any thread: nothing if @p layout feeds every input of @p shader. */
    [[nodiscard]] static std::optional<MissingInput> evaluate(const VertexLayout& layout,
                                                              const ShaderInfo&   shader);

    /**
     * Pure, any thread. The sentence that names @p mesh with its layout, @p shader and @p missing:
     * `mesh "quad" (float2 @0, float2 @8) does not provide location 2 (vec3 inNormal) of shader
     * "Lit"`.
     */
    [[nodiscard]] static std::string
    describe(const MeshData& mesh, const ShaderInfo& shader, const MissingInput& missing);

    /**
     * Checks the pair, from the verdict kept for it or by evaluating it.
     * @throws DrawError if @p mesh cannot feed @p shader.
     */
    void require(const MeshData& mesh, const ShaderInfo& shader);

private:
    /** Lets the tests count the verdicts. */
    friend struct DrawCheckTestAccess;

    /** Key: layoutId and programId. The value is empty when the pair is compatible. */
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::optional<MissingInput>> mVerdicts;
};
} // namespace Detail
} // namespace VOG::Engine
