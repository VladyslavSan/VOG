#include "VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp"

namespace VOG::Graphics::Vulkan
{
AcquiredSwapchainImage::AcquiredSwapchainImage(vk::Image                            image,
                                               std::shared_ptr<vk::raii::ImageView> imageView,
                                               vk::Format                           format,
                                               vk::Extent3D                         extent)
    : AttachmentInterface{AttachmentUsage::eColor, format, extent, SampleCount::e1}
    , mImage{image}
    , mImageView{std::move(imageView)}
{
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

} // namespace VOG::Graphics::Vulkan
