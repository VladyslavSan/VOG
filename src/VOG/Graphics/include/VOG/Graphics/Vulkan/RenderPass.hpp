#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <boost/container/small_vector.hpp>

namespace VOG::Graphics::Vulkan
{
class Device;
class RenderPass : public vk::raii::RenderPass
{
public:
    using AttachmentsDescription  = vk::AttachmentDescription;
    using AttachmentsDescriptions = boost::container::small_vector<AttachmentsDescription, 3>;
    using AttachmentsDescriptionsInternal =
        boost::container::small_vector<AttachmentsDescription, 4>;

    static std::shared_ptr<RenderPass>
    create(const Device&                  device,
           const AttachmentsDescriptions& colorAttachments,
           const AttachmentsDescription&  depthStencil)
    {
        return std::make_shared<RenderPass>(device, colorAttachments, depthStencil);
    }

    RenderPass(const Device&                  device,
               const AttachmentsDescriptions& colorAttachments,
               const AttachmentsDescription&  depthStencil);

protected:
    AttachmentsDescriptionsInternal mAttachmentDescriptions;
    const bool                      mDepthStencilProvided;
};
} // namespace VOG::Graphics::Vulkan
