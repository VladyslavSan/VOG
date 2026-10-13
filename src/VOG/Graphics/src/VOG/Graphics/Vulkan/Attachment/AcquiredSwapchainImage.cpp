#include "VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp"

#include <VOG/Common/Assert.hpp>

namespace VOG::Graphics::Vulkan
{
AcquiredSwapchainImage::AcquiredSwapchainImage(
    vk::Image                            image,
    std::shared_ptr<vk::raii::ImageView> imageView,
    vk::Format                           format,
    vk::Extent3D                         extent,
    const vk::raii::Semaphore*           imageAvailableSemaphore)
    : AttachmentInterface{AttachmentUsage::eColor, format, extent, SampleCount::e1}
    , mImage{image}
    , mImageView{std::move(imageView)}
    , mImageAvailableSemaphore{imageAvailableSemaphore}
{
    VOG_ASSERT_MSG(mImageAvailableSemaphore, "Acquire must provide an image-available semaphore.");
}

const vk::Image&
AcquiredSwapchainImage::getImage() const
{
    return mImage;
}

const std::shared_ptr<vk::raii::ImageView>&
AcquiredSwapchainImage::getImageView() const
{
    return mImageView;
}

const vk::raii::Semaphore&
AcquiredSwapchainImage::getImageAvailableSemaphore() const
{
    return *mImageAvailableSemaphore;
}
} // namespace VOG::Graphics::Vulkan
