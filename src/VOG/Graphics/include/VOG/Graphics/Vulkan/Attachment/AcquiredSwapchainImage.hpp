#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

#include <memory>

namespace VOG::Graphics::Vulkan
{
/**
 * Attachment for one acquired swapchain image. It shares ownership of the image view, so it stays
 * valid for recording; its identity is stable while the swapchain keeps acquiring other images.
 */
class AcquiredSwapchainImage final : public AttachmentInterface
{
public:
    AcquiredSwapchainImage(vk::Image                            image,
                           std::shared_ptr<vk::raii::ImageView> imageView,
                           vk::Format                           format,
                           vk::Extent3D                         extent);

    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

private:
    vk::Image                            mImage;
    std::shared_ptr<vk::raii::ImageView> mImageView;
};

VOG_DECLARE_PTR(AcquiredSwapchainImage);
} // namespace VOG::Graphics::Vulkan
