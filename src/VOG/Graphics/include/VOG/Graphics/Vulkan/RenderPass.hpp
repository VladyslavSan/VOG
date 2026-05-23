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
public:
    using AttachmentsDescription = vk::AttachmentDescription;
    using AttachmentsDescriptions =
        StaticVector<AttachmentsDescription, Limits::gMaxNumAttachments - 1u>;
    using AttachmentsDescriptionsInternal =
        StaticVector<AttachmentsDescription, Limits::gMaxNumAttachments>;

    static std::shared_ptr<RenderPass>
    create(DevicePtr                      device,
           const AttachmentsDescriptions& colorAttachments,
           const AttachmentsDescription&  depthStencil)
    {
        return std::make_shared<RenderPass>(std::move(device), colorAttachments, depthStencil);
    }

    RenderPass(DevicePtr                      device,
               const AttachmentsDescriptions& colorAttachments,
               const AttachmentsDescription&  depthStencil);

    RenderpassDescription getRenderpassDescription() const;

protected:
    const DevicePtr                 mDevice;
    AttachmentsDescriptionsInternal mAttachmentDescriptions;
    const bool                      mDepthStencilProvided;
};
} // namespace VOG::Graphics::Vulkan
