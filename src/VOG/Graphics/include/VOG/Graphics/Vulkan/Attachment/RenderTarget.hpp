#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

namespace VOG::Graphics::Vulkan
{
/**
 * A 2D, single-mip, single-sample image in memory that is preferably device-local, with a view of
 * all its aspects, that a command buffer renders into: the other attachment beside the swapchain
 * image. Made by Device::createRenderTarget(). The image is undefined until a barrier moves it to
 * the layout of its first use. The target keeps the device alive; a command buffer that records it
 * keeps it alive until the GPU is done.
 */
class RenderTarget final
    : public AttachmentInterface
    , protected MemoryAllocator::Allocation
    , private vk::raii::Image
{
    friend class MemoryAllocator;

public:
    const vk::Image&           getImage() const override;
    const vk::raii::ImageView& getImageView() const override;

private:
    RenderTarget(MemoryAllocator::Allocation allocation,
                 vk::raii::Image             image,
                 vk::raii::ImageView         view,
                 vk::Format                  format,
                 vk::Extent2D                extent);

    /** Declared after the image, so it is destroyed before it. */
    vk::raii::ImageView mView;
};

VOG_DECLARE_PTR(RenderTarget);
} // namespace VOG::Graphics::Vulkan
