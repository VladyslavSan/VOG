#include "VOG/Graphics/Vulkan/FrameBuffer.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <array>

namespace VOG::Graphics::Vulkan
{
namespace
{
vk::Extent2D
validateAndGetFramebufferDims(const Framebuffer::AttachmentRefs& attachments)
{
    VOG_ASSERT_MSG(attachments.size() > 0,
                   "Framebuffer is being created without color attachments provided");
    VOG_ASSERT_MSG(attachments.size() <= 4,
                   "Framebuffer should be created with up to 4 attachments but no more.");
    const auto extent = attachments[0]->getExtent();

    return {.width = extent.width, .height = extent.height};
}
} // namespace

Framebuffer::Framebuffer(const Device&               device,
                         const AttachmentRefs&       attachments,
                         const vk::raii::RenderPass& renderPass)
    : vk::raii::Framebuffer{nullptr}
    , mAttachments{attachments}
    , mSize{validateAndGetFramebufferDims(mAttachments)}
{
    std::array<vk::ImageView, 4> imageViews{};
    std::uint8_t                 imageCount = 0u;
    for (const auto& attachment : mAttachments)
    {
        imageViews[imageCount] = **attachment->getImageView();
        ++imageCount;
    }

    vk::FramebufferCreateInfo createInfo{.renderPass      = *renderPass,
                                         .attachmentCount = imageCount,
                                         .pAttachments    = imageViews.data(),
                                         .width           = mSize.width,
                                         .height          = mSize.height,
                                         .layers          = 1u};

    static_cast<vk::raii::Framebuffer&>(*this) = vk::raii::Framebuffer{device, createInfo};
}
} // namespace VOG::Graphics::Vulkan
