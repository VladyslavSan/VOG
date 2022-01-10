#include "VOG/Graphics/Vulkan/RenderPass.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

namespace VOG::Graphics::Vulkan
{
RenderPass::RenderPass(const Device&                  device,
                       const AttachmentsDescriptions& colorAttachments,
                       const AttachmentsDescription&  depthStencil)
    : vk::raii::RenderPass(nullptr)
    , mAttachmentDescriptions{colorAttachments.begin(), colorAttachments.end()}
    , mDepthStencilProvided{depthStencil.format != vk::Format::eUndefined}
{
    if (mDepthStencilProvided)
    {
        mAttachmentDescriptions.push_back(depthStencil);
    }

    VOG_ASSERT(mAttachmentDescriptions.size() > 0);
    VOG_ASSERT(mAttachmentDescriptions.size() <= 4);

    std::array<vk::AttachmentReference, 4> attachmentReference;

    for (std::uint32_t i = 0; i < mAttachmentDescriptions.size(); ++i)
    {
        const auto& attachmentDescription = mAttachmentDescriptions[i];
        VOG_ASSERT(attachmentDescription.format != vk::Format::eUndefined);

        attachmentReference[i] = {.attachment = i, .layout = attachmentDescription.finalLayout};
    }

    const std::uint32_t attachmentsCount =
        static_cast<std::uint32_t>(mAttachmentDescriptions.size());
    const std::uint32_t colorAttachmentsCount =
        mDepthStencilProvided ? attachmentsCount - 1 : attachmentsCount;
    vk::SubpassDescription subpass = {.pipelineBindPoint    = vk::PipelineBindPoint::eGraphics,
                                      .colorAttachmentCount = colorAttachmentsCount,
                                      .pColorAttachments    = attachmentReference.data()};
    if (mDepthStencilProvided)
    {
        subpass.setPDepthStencilAttachment(attachmentReference.data() + colorAttachmentsCount);
    }

    {
        vk::RenderPassCreateInfo ci = {.attachmentCount = attachmentsCount,
                                       .pAttachments    = mAttachmentDescriptions.data(),
                                       .subpassCount    = 1,
                                       .pSubpasses      = &subpass};

        static_cast<vk::raii::RenderPass&>(*this) = {device, ci};
    }
}
} // namespace VOG::Graphics::Vulkan
