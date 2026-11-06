#include "Assets.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Rally
{
namespace
{
constexpr std::size_t gBytesPerPixel = 4u;

/** A pixel of the image, as its first byte. */
std::byte*
pixelAt(Image& image, std::uint32_t column, std::uint32_t row)
{
    return image.pixels.data() +
           (((std::size_t{row} * image.extent.width) + column) * gBytesPerPixel);
}

/** A color of four 8-bit channels. */
struct Color
{
    std::uint8_t red;
    std::uint8_t green;
    std::uint8_t blue;
    std::uint8_t alpha;
};

void
put(std::byte* pixel, const Color& color)
{
    pixel[0] = std::byte{color.red};
    pixel[1] = std::byte{color.green};
    pixel[2] = std::byte{color.blue};
    pixel[3] = std::byte{color.alpha};
}

Image
blank(std::uint32_t width, std::uint32_t height)
{
    return {.extent = {.width = width, .height = height},
            .pixels = std::vector<std::byte>(std::size_t{width} * height * gBytesPerPixel)};
}
} // namespace

// The numbers below are the art of the textures: colors, sizes of stripes.
// NOLINTBEGIN(readability-magic-numbers)
Image
makeWhitePixel()
{
    Image image = blank(1u, 1u);
    put(pixelAt(image, 0u, 0u), {255u, 255u, 255u, 255u});

    return image;
}

Image
makeCourt()
{
    constexpr std::uint32_t kWidth  = 160u;
    constexpr std::uint32_t kHeight = 90u;

    Image image = blank(kWidth, kHeight);
    for (std::uint32_t row = 0u; row < kHeight; ++row)
    {
        for (std::uint32_t column = 0u; column < kWidth; ++column)
        {
            const bool border =
                column == 0u || row == 0u || column == kWidth - 1u || row == kHeight - 1u;
            // Light and dark bands, with a whole band across the middle: no edge there. A little
            // brighter toward the middle of the court.
            const bool band = (((column + 5u) / 10u) % 2u) == 0u;

            std::uint8_t base = band ? 34u : 28u;
            base =
                static_cast<std::uint8_t>(base + ((row < kHeight / 2u ? row : kHeight - row) / 6u));
            if (border)
            {
                put(pixelAt(image, column, row), {200u, 220u, 215u, 255u});
            }
            else
            {
                put(pixelAt(image, column, row),
                    {.red   = base,
                     .green = static_cast<std::uint8_t>(base + 48u),
                     .blue  = static_cast<std::uint8_t>(base + 46u),
                     .alpha = 255u});
            }
        }
    }

    return image;
}

Image
makePaddle()
{
    constexpr std::uint32_t kSize = 8u;

    Image image = blank(kSize, kSize);
    for (std::uint32_t row = 0u; row < kSize; ++row)
    {
        for (std::uint32_t column = 0u; column < kSize; ++column)
        {
            const bool         light = ((row / 2u) % 2u) == 0u;
            const std::uint8_t shade = light ? 255u : 205u;
            put(pixelAt(image, column, row), {shade, shade, shade, 255u});
        }
    }

    return image;
}

Image
makeBumper()
{
    constexpr std::uint32_t kSize = 16u;

    // Diagonal stripes of two shades, and a dark frame around them.
    Image image = blank(kSize, kSize);
    for (std::uint32_t row = 0u; row < kSize; ++row)
    {
        for (std::uint32_t column = 0u; column < kSize; ++column)
        {
            const bool frame =
                column == 0u || row == 0u || column == kSize - 1u || row == kSize - 1u;
            const bool light = ((column + row) / 4u) % 2u == 0u;
            if (frame)
            {
                put(pixelAt(image, column, row), {60u, 30u, 90u, 255u});
            }
            else if (light)
            {
                put(pixelAt(image, column, row), {255u, 255u, 255u, 255u});
            }
            else
            {
                put(pixelAt(image, column, row), {170u, 170u, 200u, 255u});
            }
        }
    }

    return image;
}

Image
makeBall()
{
    constexpr std::uint32_t kSize = 64u;

    Image image = blank(kSize, kSize);
    for (std::uint32_t row = 0u; row < kSize; ++row)
    {
        for (std::uint32_t column = 0u; column < kSize; ++column)
        {
            const float across = (((static_cast<float>(column) + 0.5f) / kSize) * 2.0f) - 1.0f;
            const float down   = (((static_cast<float>(row) + 0.5f) / kSize) * 2.0f) - 1.0f;
            const float radius = std::sqrt((across * across) + (down * down));
            // Solid up to 0.6 of the radius, then fading to nothing at the edge.
            const float coverage = std::clamp((1.0f - radius) / 0.4f, 0.0f, 1.0f);
            const float shade    = 0.75f + (0.25f * (1.0f - radius));
            put(pixelAt(image, column, row),
                {.red   = static_cast<std::uint8_t>(255.0f * shade),
                 .green = static_cast<std::uint8_t>(235.0f * shade),
                 .blue  = static_cast<std::uint8_t>(190.0f * shade),
                 .alpha = static_cast<std::uint8_t>(255.0f * coverage)});
        }
    }

    return image;
}

// NOLINTEND(readability-magic-numbers)

VOG::Engine::VertexLayout
quadLayout()
{
    return VOG::Engine::vertexLayout(
        sizeof(QuadVertex),
        {VOG::Engine::attribute<VOG::Math::Vector2f>(offsetof(QuadVertex, position)),
         VOG::Engine::attribute<VOG::Math::Vector2f>(offsetof(QuadVertex, uv))});
}
} // namespace Rally
