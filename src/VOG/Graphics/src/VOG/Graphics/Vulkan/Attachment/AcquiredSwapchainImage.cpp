#include "VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/InDestructor.hpp>

namespace VOG::Graphics::Vulkan
{
AcquiredSwapchainImage::AcquiredSwapchainImage(SwapchainGenerationPtr generation,
                                               std::uint32_t          imageIndex,
                                               vk::Format             format)
    : AttachmentInterface{AttachmentUsage::eColor, format, generation->extent, SampleCount::e1}
    , mGeneration{std::move(generation)}
    , mImageIndex{imageIndex}
    , mImage{mGeneration->images.at(imageIndex).image}
    // Aliasing: the view is owned by the generation, and this pointer keeps the generation alive.
    , mImageView{mGeneration, &mGeneration->images.at(imageIndex).imageView}
{
}

AcquiredSwapchainImage::~AcquiredSwapchainImage()
{
    if (mPresented)
    {
        return;
    }

    inDestructor("vkReleaseSwapchainImagesEXT",
                 [this]
                 {
                     const std::uint32_t                     imageIndex = mImageIndex;
                     const vk::ReleaseSwapchainImagesInfoKHR releaseInfo{
                         .swapchain       = *mGeneration->swapchain,
                         .imageIndexCount = 1u,
                         .pImageIndices   = &imageIndex,
                     };
                     mGeneration->device->releaseSwapchainImagesEXT(releaseInfo);
                 });
}

void
AcquiredSwapchainImage::markPresented()
{
    mPresented = true;
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
