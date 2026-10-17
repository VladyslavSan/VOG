#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace VOG::Engine
{
/** Formats of one vertex attribute: float components, tightly packed. */
enum class VertexFormat : std::uint8_t
{
    eFloat2,
    eFloat4
};

struct VertexAttribute
{
    VertexFormat format;
    /** Offset in bytes from the start of a vertex. */
    std::uint32_t offset;

    auto operator<=>(const VertexAttribute&) const = default;
};

/** Interleaved vertices of a triangle list, drawn in order or through indices. */
struct MeshDescription
{
    /** Copied by Renderer::createMesh(); the caller keeps ownership. */
    std::span<const std::byte> vertices;
    /** Optional; copied like the vertices. Three per triangle, each less than the vertex count.
     *  Empty draws the vertices in order. */
    std::span<const std::uint32_t> indices;
    /** Size of one vertex in bytes. */
    std::uint32_t stride = 0u;
    /** The shader input location of an attribute is its index here. */
    std::vector<VertexAttribute> attributes;
};

/** Immutable after creation; made by Renderer::createMesh(). */
class Mesh;
using MeshPtr = std::shared_ptr<const Mesh>;
} // namespace VOG::Engine
