#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
using SampleCount = vk::SampleCountFlagBits;

class AttachmentInterface
{
public:
    AttachmentInterface(vk::Format format, vk::Extent3D extent, SampleCount sampleCount);

    virtual ~AttachmentInterface() = default;

    vk::Format getFormat() const;

    vk::Extent3D getExtent() const;

    vk::Extent2D getExtent2D() const;

    SampleCount getNumSamples() const;

    virtual const vk::Image&           getImage() const     = 0;
    virtual const vk::raii::ImageView& getImageView() const = 0;

protected:
    vk::Format   mFormat = vk::Format::eUndefined;
    vk::Extent3D mExtent;
    SampleCount  mSampleCount;
};

inline AttachmentInterface::AttachmentInterface(vk::Format              format,
                                                vk::Extent3D            extent,
                                                vk::SampleCountFlagBits sampleCount)
    : mFormat{format}
    , mExtent{extent}
    , mSampleCount(sampleCount)
{
}

inline vk::Format
AttachmentInterface::getFormat() const
{
    return mFormat;
}

inline vk::Extent3D
AttachmentInterface::getExtent() const
{
    return mExtent;
}

inline vk::Extent2D
AttachmentInterface::getExtent2D() const
{
    return {.width = mExtent.width, .height = mExtent.height};
}

inline SampleCount
AttachmentInterface::getNumSamples() const
{
    return mSampleCount;
}

VOG_DECLARE_PTR(AttachmentInterface);
} // namespace VOG::Graphics::Vulkan
