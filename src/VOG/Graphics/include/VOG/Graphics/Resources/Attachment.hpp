#pragma once

#include <cstdint>
#include <memory>
#include <tuple>

namespace vk::raii
{
class ImageView;
}

namespace VOG::Graphics::Resources
{
enum class AttachmentUsage : std::uint8_t
{
    Color,
    Depth,
    Stencil,
    DepthStencil,
    Sampled
};

using AttachmentFormat = std::uint32_t;
inline AttachmentFormat UnknownFormat = 0;

struct AttachmentExtent
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 0;

    auto operator<=>(const AttachmentExtent&) const = default;
};

enum class SampleCount : std::uint8_t
{
    e1,
    e2,
    e4,
    e8,
    e16
};

class Attachment
{
public:
    Attachment(AttachmentUsage usage, AttachmentFormat format, AttachmentExtent extent,
               SampleCount sampleCount);

    virtual AttachmentUsage GetUsage() const;

    virtual AttachmentFormat GetFormat() const;

    virtual AttachmentExtent GetExtent() const;

    virtual SampleCount GetNumSamples() const;

    virtual void Recreate(AttachmentExtent newExtent);

    virtual const std::shared_ptr<vk::raii::ImageView>& GetImageView() const = 0;

protected:
    AttachmentUsage m_usage;
    AttachmentFormat m_format;
    AttachmentExtent m_extent;
    SampleCount m_sampleCount;
};

} // namespace VOG::Graphics::Resources
