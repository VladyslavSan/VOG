#include "VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spdlog/spdlog.h>

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

// Releasing can only throw on device loss; there is nothing left to do about that here.
AcquiredSwapchainImage::~AcquiredSwapchainImage() // NOLINT(bugprone-exception-escape)
{
    if (mPresented)
    {
        return;
    }

    try
    {
        const std::uint32_t                     imageIndex = mImageIndex;
        const vk::ReleaseSwapchainImagesInfoKHR releaseInfo{
            .swapchain       = *mGeneration->swapchain,
            .imageIndexCount = 1u,
            .pImageIndices   = &imageIndex,
        };
        mGeneration->device->releaseSwapchainImagesEXT(releaseInfo);
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Releasing a swapchain image failed: {}", exception.what());
    }
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
