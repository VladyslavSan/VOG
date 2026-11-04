#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

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

namespace Detail
{
class TextureData;
} // namespace Detail

class Renderer;

/**
 * Immutable after creation; made by Renderer::createTexture(). A copy shares the texture with the
 * original. A default-constructed Texture is empty: it is no texture, and
 * Renderer::createMaterial() rejects it.
 */
class Texture
{
public:
    Texture() = default;

    /** True for a texture that createTexture() made. */
    explicit
    operator bool() const
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Texture&) const = default;

private:
    friend class Renderer;
    friend struct HandleTestAccess;

    explicit Texture(std::shared_ptr<const Detail::TextureData> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::TextureData> mData;
};
} // namespace VOG::Engine
