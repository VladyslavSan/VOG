#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Rally
{
/** RGBA pixels of an image, rows from the top. */
struct Image
{
    VOG::Engine::Extent    extent;
    std::vector<std::byte> pixels;
};

/** One white pixel: the texture of everything that is only a color, with a tint. */
Image makeWhitePixel();

/** The court: a dark teal floor, light stripes and a border. It is small and drawn magnified with
 *  the nearest filter, which makes its pixels large and crisp. */
Image makeCourt();

/** A paddle: bands of two shades across its width, for the nearest filter. */
Image makePaddle();

/** A bumper: diagonal stripes of two shades in a dark frame, for the nearest filter. */
Image makeBumper();

/** A ball: a soft disc, opaque in the middle and clear at the edge, for the linear filter and a
 *  blended material. */
Image makeBall();

/** The vertex of the unit square that every object of the game is drawn with. */
struct QuadVertex
{
    std::array<float, 2> position;
    std::array<float, 2> uv;
};

/** A square of side 2 around the origin, clockwise for a viewer, with uv row 0 at the top. */
inline constexpr std::array<QuadVertex, 4> gQuadVertices{{
    {.position = {-1.0f, -1.0f}, .uv = {0.0f, 1.0f}},
    {.position = {-1.0f, 1.0f}, .uv = {0.0f, 0.0f}},
    {.position = {1.0f, 1.0f}, .uv = {1.0f, 0.0f}},
    {.position = {1.0f, -1.0f}, .uv = {1.0f, 1.0f}},
}};

inline constexpr std::array<std::uint32_t, 6> gQuadIndices{0u, 1u, 2u, 2u, 3u, 0u};

/** The layout of QuadVertex. */
VOG::Engine::VertexLayout quadLayout();
} // namespace Rally
