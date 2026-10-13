#include "VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp"

#include <VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/InDestructor.hpp>

namespace VOG::Graphics::Vulkan
{
AcquiredSwapchainImage::AcquiredSwapchainImage(SwapchainGenerationPtr generation,
                                               std::uint32_t          imageIndex,
                                               SemaphorePtr           imageAvailable)
    : AttachmentInterface{AttachmentUsage::eColor,
                          generation->format,
                          vk::Extent3D{.width  = generation->extent.width,
                                       .height = generation->extent.height,
                                       .depth  = 1u},
                          SampleCount::e1}
    , mGeneration{std::move(generation)}
    , mImageIndex{imageIndex}
    , mImageAvailable{std::move(imageAvailable)}
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

const SemaphorePtr&
AcquiredSwapchainImage::imageAvailableSemaphore() const
{
    return mImageAvailable;
}

const vk::Image&
AcquiredSwapchainImage::getImage() const
{
    return mGeneration->images.at(mImageIndex).image;
}

const vk::raii::ImageView&
AcquiredSwapchainImage::getImageView() const
{
    return mGeneration->images.at(mImageIndex).view;
}
} // namespace VOG::Graphics::Vulkan
