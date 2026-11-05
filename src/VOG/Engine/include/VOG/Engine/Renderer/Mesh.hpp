#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace VOG::Engine
{
/** Formats of one vertex attribute: float components, tightly packed. */
enum class VertexFormat : std::uint8_t
{
    eFloat2,
    eFloat3,
    eFloat4
};

struct VertexAttribute
{
    VertexFormat format = VertexFormat::eFloat2;
    /** Offset in bytes from the start of a vertex. */
    std::uint32_t offset = 0u;

    auto operator<=>(const VertexAttribute&) const = default;
};

/** A pipeline takes at most this many vertex inputs. */
inline constexpr std::size_t gMaxVertexAttributes = 8u;

/**
 * How the bytes of a vertex are laid out: the size of a vertex and its attributes. The shader
 * input location of an attribute is its index. The attributes are an array with a count, so that
 * a layout is a literal type: it can be a constexpr constant, it is trivially copyable, and it is
 * the whole key of the renderer's tables of layouts and pipelines.
 */
struct VertexLayout
{
    /** Size of one vertex in bytes. */
    std::uint32_t stride = 0u;
    /** How many of #attributes are in use; at most gMaxVertexAttributes. */
    std::uint8_t                                      attributeCount = 0u;
    std::array<VertexAttribute, gMaxVertexAttributes> attributes{};

    /** The attributes in use, in location order. */
    [[nodiscard]] constexpr std::span<const VertexAttribute>
    inputs() const noexcept
    {
        return std::span{attributes}.first(
            std::min<std::size_t>(attributeCount, gMaxVertexAttributes));
    }

    auto operator<=>(const VertexLayout&) const = default;
};

/**
 * The layout of vertices of @p stride bytes with @p attributes, in location order.
 * @throws std::invalid_argument for more than gMaxVertexAttributes attributes (a compile error in
 *         a constant expression).
 */
constexpr VertexLayout
vertexLayout(std::uint32_t stride, std::initializer_list<VertexAttribute> attributes)
{
    if (attributes.size() > gMaxVertexAttributes)
    {
        throw std::invalid_argument{
            "A vertex layout takes at most gMaxVertexAttributes attributes."};
    }

    VertexLayout layout{.stride         = stride,
                        .attributeCount = static_cast<std::uint8_t>(attributes.size())};
    std::ranges::copy(attributes, layout.attributes.begin());

    return layout;
}

/** Interleaved vertices of a triangle list, drawn in order or through indices. */
struct MeshDescription
{
    /** Names the mesh in error messages; may be empty. */
    std::string name;
    /** Copied by Renderer::createMesh(); the caller keeps ownership. */
    std::span<const std::byte> vertices;
    /** Optional; copied like the vertices. Three per triangle, each less than the vertex count.
     *  Empty draws the vertices in order. */
    std::span<const std::uint32_t> indices;
    VertexLayout                   layout;
};

namespace Detail
{
class MeshData;
} // namespace Detail

class Renderer;

/**
 * Immutable after creation; made by Renderer::createMesh(). A copy shares the mesh with the
 * original. A default-constructed Mesh is empty: it is no mesh, and a draw of it is skipped.
 */
class Mesh
{
public:
    Mesh() = default;

    /** True for a mesh that createMesh() made. */
    explicit
    operator bool() const noexcept
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Mesh&) const = default;

    /**
     * True once the copy of the mesh to its GPU memory has finished. Any thread, also after the
     * Renderer was destroyed. A draw before then is still correct: the frame waits for the copy on
     * the GPU. For progress bars and placeholder swaps, not for correctness. False for an empty
     * mesh, and for ever for a mesh whose copy never ran: every handle was dropped before it, or
     * the Renderer was destroyed before it.
     * @throws std::runtime_error if the device cannot answer (device loss).
     */
    [[nodiscard]] bool isResident() const;

private:
    friend class Renderer;
    friend class ResourceFactory;
    friend class RenderQueue;
    friend struct DrawRecording;
    friend class FrameLoop;
    friend struct HandleTestAccess;

    explicit Mesh(std::shared_ptr<const Detail::MeshData> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::MeshData> mData;
};
} // namespace VOG::Engine
