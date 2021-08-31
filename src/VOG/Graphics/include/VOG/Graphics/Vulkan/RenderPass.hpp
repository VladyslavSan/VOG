#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(AttachmentInterface);
class Device;
class RenderPass
{
public:
    struct ColorAttachmentInfo
    {
        const AttachmentInterfacePtr attachment = {};
        vk::AttachmentLoadOp         loadOp     = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp        storeOp    = vk::AttachmentStoreOp::eDontCare;
        vk::ClearColorValue          clearColor = {};
    };

    struct DepthStencilAttachmentInfo
    {
        const AttachmentInterfacePtr attachment        = {};
        vk::AttachmentLoadOp         loadOp            = vk::AttachmentLoadOp::eDontCare;
        vk::AttachmentStoreOp        storeOp           = vk::AttachmentStoreOp::eDontCare;
        vk::ClearDepthStencilValue   clearDepthStencil = {};
    };

    static std::shared_ptr<RenderPass>
    create(const Device&              device,
           ColorAttachmentInfo        mainColor,
           DepthStencilAttachmentInfo depthStencil)
    {
        return std::make_shared<RenderPass>(device, mainColor, depthStencil);
    }

    RenderPass(const Device&              device,
               ColorAttachmentInfo        mainColor,
               DepthStencilAttachmentInfo depthstencil);

    vk::RenderPass
    getRenderPassHandle() const
    {
        return *mRenderPass;
    }

    vk::RenderPassBeginInfo getBeginInfo() const;

protected:
    ColorAttachmentInfo        mColorAttachment;
    DepthStencilAttachmentInfo mDepthStencilAttachment;
    const vk::ClearValue       mClearValues[2];

    vk::raii::RenderPass  mRenderPass;
    vk::raii::Framebuffer mFramebuffer;
};
} // namespace VOG::Graphics::Vulkan
