#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(AttachmentInterface);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(RenderPass);
class Framebuffer : public vk::raii::Framebuffer
{
    friend class Device;

public:
    using AttachmentPtr       = AttachmentInterfacePtr;
    using ColorAttachmentPtrs = StaticVector<AttachmentPtr, Limits::gMaxNumAttachments - 1u>;
    using AttachmentPtrs      = StaticVector<AttachmentPtr, Limits::gMaxNumAttachments>;

private:
    Framebuffer(DevicePtr           device,
                RenderPassPtr       renderPass,
                ColorAttachmentPtrs colorAttachments,
                AttachmentPtr       depthStencilAttachment);

public:
    vk::Extent2D extent() const;

    RenderpassDescription getRenderpassDescription() const;

protected:
    DevicePtr      mDevice;
    RenderPassPtr  mRenderPass;
    bool           mHasDepthStencilAttachment;
    AttachmentPtrs mAttachments;
    vk::Extent2D   mExtent;
};
} // namespace VOG::Graphics::Vulkan
