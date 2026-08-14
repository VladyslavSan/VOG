#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

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

    virtual ~AttachmentInterface() = default;

    AttachmentUsage getUsage() const;

    vk::Format getFormat() const;

    vk::Extent3D getExtent() const;

    vk::Extent2D getExtent2D() const;

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
