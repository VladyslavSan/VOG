#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <boost/container/small_vector.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(AttachmentInterface);
class Device;
class Framebuffer : public vk::raii::Framebuffer
{
public:
    static constexpr std::uint8_t kMaxNumAttachments = 4u;
    using AttachmentRef                              = AttachmentInterfacePtr;
    using AttachmentRefs = boost::container::small_vector<AttachmentRef, kMaxNumAttachments>;

    static std::shared_ptr<Framebuffer>
    create(const Device&               device,
           const AttachmentRefs&       attachments,
           const vk::raii::RenderPass& renderPass)
    {
        return std::make_shared<Framebuffer>(device, attachments, renderPass);
    }

    Framebuffer(const Device&               device,
                const AttachmentRefs&       attachments,
                const vk::raii::RenderPass& renderPass);

    vk::Extent2D
    size() const
    {
        return mSize;
    }

protected:
    const AttachmentRefs mAttachments;
    const vk::Extent2D   mSize;
};
} // namespace VOG::Graphics::Vulkan
