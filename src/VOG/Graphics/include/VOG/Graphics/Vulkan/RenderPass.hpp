#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class RenderPass : public vk::raii::RenderPass
{
    friend class Device;

public:
    using AttachmentsDescription = vk::AttachmentDescription;
    using AttachmentsDescriptions =
        StaticVector<AttachmentsDescription, Limits::gMaxNumAttachments - 1u>;
    using AttachmentsDescriptionsInternal =
        StaticVector<AttachmentsDescription, Limits::gMaxNumAttachments>;

private:
    RenderPass(DevicePtr                      device,
               const AttachmentsDescriptions& colorAttachments,
               const AttachmentsDescription&  depthStencil);

public:
    RenderpassDescription getRenderpassDescription() const;

protected:
    const DevicePtr                 mDevice;
    AttachmentsDescriptionsInternal mAttachmentDescriptions;
    const bool                      mDepthStencilProvided;
};
} // namespace VOG::Graphics::Vulkan
