#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

#include <memory>

namespace VOG::Graphics::Vulkan
{
/**
 * Stable attachment for one acquired swapchain image. Prefer this over treating
 * Swapchain itself as AttachmentInterface (whose getImage/getImageView mutate with acquire).
 */
class AcquiredSwapchainImage final : public AttachmentInterface
{
public:
    AcquiredSwapchainImage(vk::Image                            image,
                           std::shared_ptr<vk::raii::ImageView> imageView,
                           vk::Format                           format,
                           vk::Extent3D                         extent,
                           const vk::raii::Semaphore*           imageAvailableSemaphore);

    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

    const vk::raii::Semaphore& getImageAvailableSemaphore() const;

private:
    vk::Image                            mImage;
    std::shared_ptr<vk::raii::ImageView> mImageView;
    const vk::raii::Semaphore*           mImageAvailableSemaphore;
};

VOG_DECLARE_PTR(AcquiredSwapchainImage);
} // namespace VOG::Graphics::Vulkan
