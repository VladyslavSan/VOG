#include "VOG/Graphics/Vulkan/RenderPass.hpp"

#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
RenderPass::RenderPass(const Device&              device,
                       ColorAttachmentInfo        mainColor,
                       DepthStencilAttachmentInfo depthstencil)
    : mColorAttachment{mainColor}
    , mDepthStencilAttachment{depthstencil}
    , mClearValues{mColorAttachment.clearColor, depthstencil.clearDepthStencil}
    , mRenderPass{nullptr}
    , mFramebuffer{nullptr}
{
    if (!mainColor.attachment)
    {
        throw std::runtime_error{"RenderPass creation failed. Main attachment is empty."};
    }

    if (depthstencil.attachment &&
        mainColor.attachment->getExtent() != depthstencil.attachment->getExtent())
    {
        throw std::runtime_error{
            "RenderPass creation failed. Color and depth attachments have different extents."};
    }

    std::array<vk::AttachmentDescription, 2> description;
    description[0].setFormat(mainColor.attachment->getFormat());
    description[0].setSamples(mainColor.attachment->getNumSamples());
    description[0].setLoadOp(mainColor.loadOp);
    description[0].setStoreOp(mainColor.storeOp);
    description[0].setInitialLayout(vk::ImageLayout::eUndefined);
    description[0].setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal);

    if (depthstencil.attachment)
    {
        description[1].setFormat(depthstencil.attachment->getFormat());
        description[1].setSamples(depthstencil.attachment->getNumSamples());
        description[1].setLoadOp(depthstencil.loadOp);
        description[1].setStoreOp(depthstencil.storeOp);
        description[1].setStencilLoadOp(depthstencil.loadOp);
        description[1].setStencilStoreOp(depthstencil.storeOp);
        description[1].setInitialLayout(vk::ImageLayout::eUndefined);
        description[1].setFinalLayout(vk::ImageLayout::eDepthAttachmentOptimal);
    }

    std::array<vk::AttachmentReference, 2> attachmentReference;
    attachmentReference[0].setAttachment(0);
    attachmentReference[0].setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    if (depthstencil.attachment)
    {
        attachmentReference[1].setAttachment(1);
        attachmentReference[1].setLayout(vk::ImageLayout::eDepthAttachmentOptimal);
    }

    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(attachmentReference[0]);
    if (depthstencil.attachment)
        subpass.setPDepthStencilAttachment(&attachmentReference[1]);

    {
        vk::RenderPassCreateInfo ci{};
        ci.setAttachments(description);
        ci.setAttachmentCount(depthstencil.attachment ? 2 : 1);
        ci.setSubpassCount(1);
        ci.setSubpasses(subpass);

        mRenderPass = {device, ci};
    }

    {
        std::array<vk::ImageView, 2> attachmentsViews = {
            **mainColor.attachment->getImageView(),
            depthstencil.attachment ? **depthstencil.attachment->getImageView() : vk::ImageView{}};

        auto extent = mainColor.attachment->getExtent();

        vk::FramebufferCreateInfo ci{};
        ci.setRenderPass(*mRenderPass)
            .setHeight(extent.height)
            .setWidth(extent.width)
            .setAttachments(attachmentsViews)
            .setAttachmentCount(depthstencil.attachment ? 2 : 1)
            .setLayers(1);

        mFramebuffer = {device, ci};
    }
}

vk::RenderPassBeginInfo
RenderPass::getBeginInfo() const
{
    const vk::ClearValue* clearValues      = mClearValues;
    std::uint32_t         clearValuesCount = 2u;
    if (mColorAttachment.loadOp != vk::AttachmentLoadOp::eClear)
    {
        clearValues += 1;
        clearValuesCount -= 1u;
    }
    if (mDepthStencilAttachment.loadOp != vk::AttachmentLoadOp::eClear)
    {
        clearValuesCount -= 1u;
    }

    const vk::Extent3D attachmentExtent = mColorAttachment.attachment->getExtent();
    const vk::Rect2D   renderArea = {{0, 0}, {attachmentExtent.width, attachmentExtent.height}};

    return {.renderPass      = *mRenderPass,
            .framebuffer     = *mFramebuffer,
            .renderArea      = renderArea,
            .clearValueCount = clearValuesCount,
            .pClearValues    = clearValues};
}
} // namespace VOG::Graphics::Vulkan
