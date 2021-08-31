#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>
#include <memory>

namespace VOG::Graphics::Vulkan
{
enum class AttachmentUsage : std::uint8_t
{
    eColor,
    eDepth,
    eStencil,
    eDepthStencil,
    eSampled
};

using SampleCount = vk::SampleCountFlagBits;

class AttachmentInterface
{
public:
    AttachmentInterface(AttachmentUsage usage,
                        vk::Format      format,
                        vk::Extent3D    extent,
                        SampleCount     sampleCount);

    AttachmentUsage getUsage() const;

    vk::Format getFormat() const;

    vk::Extent3D getExtent() const;

    SampleCount getNumSamples() const;

    virtual const vk::Image&                            getImage() const     = 0;
    virtual const std::shared_ptr<vk::raii::ImageView>& getImageView() const = 0;

protected:
    AttachmentUsage mUsage  = AttachmentUsage::eColor;
    vk::Format      mFormat = vk::Format::eUndefined;
    vk::Extent3D    mExtent;
    SampleCount     mSampleCount;
};

inline AttachmentInterface::AttachmentInterface(AttachmentUsage         usage,
                                                vk::Format              format,
                                                vk::Extent3D            extent,
                                                vk::SampleCountFlagBits sampleCount)
    : mUsage{usage}
    , mFormat{format}
    , mExtent{extent}
    , mSampleCount(sampleCount)
{
}

inline AttachmentUsage
AttachmentInterface::getUsage() const
{
    return mUsage;
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

inline SampleCount
AttachmentInterface::getNumSamples() const
{
    return mSampleCount;
}

} // namespace VOG::Graphics::Vulkan
