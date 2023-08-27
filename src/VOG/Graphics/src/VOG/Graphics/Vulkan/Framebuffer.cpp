#include "VOG/Graphics/Vulkan/FrameBuffer.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>

#include <array>

namespace VOG::Graphics::Vulkan
{
namespace
{
Framebuffer::AttachmentPtrs
makeAttachmentsVector(Framebuffer::ColorAttachmentPtrs colorAttachments,
                      Framebuffer::AttachmentPtr       depthStencilAttachment)
{
    Framebuffer::AttachmentPtrs result;
    for (std::size_t i = 0; i < colorAttachments.size(); ++i)
    {
        result.push_back(std::move(colorAttachments[i]));
    }

    if (depthStencilAttachment)
    {
        result.push_back(depthStencilAttachment);
    }

    return result;
}

RenderpassDescription
makeRenderpassDescription(const bool                         hasDepthStencilAttachment,
                          const Framebuffer::AttachmentPtrs& attachments)
{
    RenderpassDescription description;

    for (std::size_t i = 0; i < attachments.size() - hasDepthStencilAttachment; ++i)
    {
        description.colorAttachmentFormats.push_back(attachments[i]->getFormat());
    }

    if (hasDepthStencilAttachment)
    {
        const auto& depthStencilAttachmentFormat = attachments.back()->getFormat();

        description.depthAttachmentFormat = isDepthFormat(depthStencilAttachmentFormat)
                                              ? depthStencilAttachmentFormat
                                              : vk::Format::eUndefined;

        description.stencilAttachmentFormat = isStencilFormat(depthStencilAttachmentFormat)
                                                ? depthStencilAttachmentFormat
                                                : vk::Format::eUndefined;
    }

    return description;
}

vk::Extent2D
validateAndGetFramebufferDims(const Framebuffer::AttachmentPtrs& attachments)
{
    VOG_ASSERT_MSG(!attachments.empty(),
                   "Framebuffer is being created without attachments provided");

    const auto extent = attachments[0]->getExtent();
    for (std::size_t i = 1; i < attachments.size(); ++i)
    {
        if (extent != attachments[i]->getExtent())
        {
            throw std::runtime_error{"Not all attachments' extents match."};
        }
    }

    return {.width = extent.width, .height = extent.height};
}
} // namespace

Framebuffer::Framebuffer(const Device&       device,
                         const RenderPass&   renderPass,
                         ColorAttachmentPtrs colorAttachments,
                         AttachmentPtr       depthStencilAttachment)
    : vk::raii::Framebuffer{nullptr}
    , mHasDepthStencilAttachment{depthStencilAttachment}
    , mAttachments{makeAttachmentsVector(std::move(colorAttachments),
                                         std::move(depthStencilAttachment))}
    , mExtent{validateAndGetFramebufferDims(mAttachments)}
{

    StaticVector<vk::ImageView, Limits::gMaxNumAttachments> imageViews{};
    std::ranges::for_each(
        mAttachments,
        [&imageViews](vk::ImageView view) { imageViews.push_back(std::move(view)); },
        [](const auto& attachmentPtr) { return **attachmentPtr->getImageView(); });

    vk::FramebufferCreateInfo createInfo{.renderPass = *renderPass,
                                         .attachmentCount =
                                             static_cast<std::uint32_t>(imageViews.size()),
                                         .pAttachments = imageViews.data(),
                                         .width        = mExtent.width,
                                         .height       = mExtent.height,
                                         .layers       = 1u};

    static_cast<vk::raii::Framebuffer&>(*this) = vk::raii::Framebuffer{device, createInfo};
}

vk::Extent2D
Framebuffer::extent() const
{
    return mExtent;
}

RenderpassDescription
Framebuffer::getRenderpassDescription() const
{
    return makeRenderpassDescription(mHasDepthStencilAttachment, mAttachments);
}
} // namespace VOG::Graphics::Vulkan
