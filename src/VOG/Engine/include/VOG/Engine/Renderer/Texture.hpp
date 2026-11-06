#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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

/** How a shader samples a texture: between the texels of the image, or the nearest one. The image
 *  repeats at its edges. */
enum class Filter : std::uint8_t
{
    eLinear,
    eNearest
};

/** A 2D image with one mip level, rows tightly packed from the top. */
struct TextureDescription
{
    /** Names the texture in error messages; may be empty. */
    std::string name;
    /** Copied by Renderer::createTexture(); the caller keeps ownership. Exactly
     *  extent.width * extent.height * 4 bytes. */
    std::span<const std::byte> pixels;
    Extent                     extent;
    TextureFormat              format = TextureFormat::eRGBA8Srgb;
    Filter                     filter = Filter::eLinear;
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
    operator bool() const noexcept
    {
        return static_cast<bool>(mData);
    }

    bool operator==(const Texture&) const = default;

    /**
     * True once the copy of the pixels to the image has finished. Any thread, also after the
     * Renderer was destroyed. A draw before then is still correct: the frame waits for the copy on
     * the GPU. False for an empty texture, and for ever for a texture whose copy never ran.
     * @throws std::runtime_error if the device cannot answer (device loss).
     */
    [[nodiscard]] bool isResident() const;

private:
    friend class Renderer;
    friend class ResourceFactory;
    friend class FrameLoop;
    friend struct DrawRecording;
    friend struct HandleTestAccess;

    explicit Texture(std::shared_ptr<const Detail::TextureData> data)
        : mData{std::move(data)}
    {
    }

    std::shared_ptr<const Detail::TextureData> mData;
};
} // namespace VOG::Engine
