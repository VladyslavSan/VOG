#include "VOG/Graphics/Resources/Attachment.hpp"

#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics::Resources
{
static_assert(sizeof(AttachmentFormat) >= sizeof(vk::Format));

Attachment::Attachment(AttachmentUsage usage, AttachmentFormat format, AttachmentExtent extent,
                       SampleCount sampleCount)
    : m_usage{usage}
    , m_format{format}
    , m_extent{extent}
    , m_sampleCount{sampleCount}
{
}

AttachmentUsage
Attachment::GetUsage() const
{
    return m_usage;
}

AttachmentFormat
Attachment::GetFormat() const
{
    return m_format;
}

AttachmentExtent
Attachment::GetExtent() const
{
    return m_extent;
}

SampleCount
Attachment::GetNumSamples() const
{
    return m_sampleCount;
}

void
Attachment::Recreate(AttachmentExtent newExtent)
{
    // Do nothing by default, implement in derived classes.
}
} // namespace VOG::Graphics::Resources
