#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace VOG::Engine
{
enum class TextureFormat : std::uint8_t
{
    /** Four bytes per pixel; the shader reads the colors as sRGB and gets them linear. */
    eRGBA8Srgb,
    /** Four bytes per pixel; the shader reads the values as they are. */
    eRGBA8Unorm
};

/** A 2D image with one mip level, rows tightly packed from the top. */
struct TextureDescription
{
    /** Copied by Renderer::createTexture(); the caller keeps ownership. Exactly
     *  width * height * 4 bytes. */
    std::span<const std::byte> pixels;
    std::uint32_t              width  = 0u;
    std::uint32_t              height = 0u;
    TextureFormat              format = TextureFormat::eRGBA8Srgb;
};

/** Immutable after creation; made by Renderer::createTexture(). */
class Texture;
using TexturePtr = std::shared_ptr<const Texture>;
} // namespace VOG::Engine
