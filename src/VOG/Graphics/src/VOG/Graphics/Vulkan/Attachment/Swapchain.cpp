#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>
#include <VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp>

#include <iostream>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::uint64_t gAcquireTimeout = 10000u;

std::uint32_t
FindPresentQueueFamilyIndex(const GraphicsProvider&     graphicsProvider,
                            const vk::raii::SurfaceKHR& surface)
{
    {
        const std::uint32_t graphicsQueueIndex = graphicsProvider.getGraphicsQueue().familyIndex;
        if (graphicsProvider.getPhysicalDevice().getSurfaceSupportKHR(graphicsQueueIndex, *surface))
        {
            return graphicsQueueIndex;
        }
    }

    auto queueFamiliesProperty = graphicsProvider.getPhysicalDevice().getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamiliesProperty.size(); ++i)
    {
        if (graphicsProvider.getPhysicalDevice().getSurfaceSupportKHR(i, *surface))
            return i;
    }

    throw std::runtime_error("No present queue found.");

    return std::numeric_limits<std::uint32_t>::max();
}

bool
QueueFamilySupportsPresent(const vk::raii::PhysicalDevice& device,
                           std::uint32_t                   queueFamilyIndex,
                           const vk::raii::SurfaceKHR&     surface)
{
    return device.getSurfaceSupportKHR(queueFamilyIndex, *surface) != 0U;
}

std::shared_ptr<vk::raii::Queue>
CreatePresentQueue(const vk::raii::PhysicalDevice& physicalDevice,
                   const vk::raii::Device&         device,
                   const vk::raii::SurfaceKHR&     surface)
{
    auto queueFamiliesProperty = physicalDevice.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamiliesProperty.size(); ++i)
    {
        if (physicalDevice.getSurfaceSupportKHR(i, *surface))
            return std::make_shared<vk::raii::Queue>(device, i, 0);
    }

    throw std::runtime_error("CreatePresentQueue failed. No present queues found.");

    return nullptr;
}

bool
successAcquirePresentResult(vk::Result result)
{
    return (result == vk::Result::eSuccess) || (result == vk::Result::eSuboptimalKHR);
}
} // namespace

Swapchain::SwapchainImageSyncData::SwapchainImageSyncData(const GraphicsProvider& graphicsProvider)
    : fence{graphicsProvider.getDevice(), vk::FenceCreateInfo{}}
{
}

Swapchain::~Swapchain() {}

Swapchain::Swapchain(const std::shared_ptr<GraphicsProvider>& graphicsProvider,
                     const Common::JSONContainer&             parameters)
    : AttachmentInterface{AttachmentUsage::eColor, vk::Format::eUndefined, {}, SampleCount::e1}
    , mGraphicsProvider{graphicsProvider}
    , mSurface{CreateRenderSurface(mGraphicsProvider, parameters, true)}
    , mPresentQueueFamilyIndex{FindPresentQueueFamilyIndex(*mGraphicsProvider, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         graphicsProvider->getGraphicsQueue().familyIndex}
    , mPresentQueue{mGraphicsProvider->getDevice(), mPresentQueueFamilyIndex, 0}
    , mSwapchain{nullptr}
{
    auto surfaceFormats = mGraphicsProvider->getPhysicalDevice().getSurfaceFormatsKHR(**mSurface);
    auto surfaceColorFormat = vk::Format::eUndefined;
    vk::ColorSpaceKHR surfaceColorSpace;
    if (surfaceFormats.empty())
    {
        throw std::runtime_error("Surface creation failed. Could not retrieve surface formats.");
    }

    mSurfaceFormat     = surfaceFormats[0];
    mFormat            = mSurfaceFormat.format;
    surfaceColorFormat = surfaceFormats[0].format;
    surfaceColorSpace  = surfaceFormats[0].colorSpace;

    auto surfaceCapabilities =
        mGraphicsProvider->getPhysicalDevice().getSurfaceCapabilitiesKHR(**mSurface);
    auto surfacePresentModes =
        mGraphicsProvider->getPhysicalDevice().getSurfacePresentModesKHR(**mSurface);

    vk::Extent2D surfaceSize;
    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max() &&
        surfaceCapabilities.currentExtent.height != std::numeric_limits<uint32_t>::max())
    {
        surfaceSize = surfaceCapabilities.currentExtent;
    }
    else
    {
        throw std::runtime_error("Surface creation failed. Surface extents are invalid.");
    }

    mExtent.width  = surfaceSize.width;
    mExtent.height = surfaceSize.height;
    mExtent.depth  = 1;

    vk::PresentModeKHR presentMode              = vk::PresentModeKHR::eImmediate;
    const std::string  preferedPresentationMode = parameters["present-mode"].getOr<std::string>("");
    if (!preferedPresentationMode.empty())
    {
        std::cout << "Requested presentation mode :" << preferedPresentationMode << std::endl;
        bool foundPresentMode = false;
        for (auto& pm : surfacePresentModes)
        {
            if (preferedPresentationMode == vk::to_string(pm))
            {
                foundPresentMode = true;
                presentMode      = pm;
            }
        }
        if (!foundPresentMode)
        {
            std::cout << "Requested presentation mode was not found" << std::endl;
        }
    }
    else
    {
        for (auto& pm : surfacePresentModes)
        {
            if (pm == vk::PresentModeKHR::eMailbox)
            {
                presentMode = vk::PresentModeKHR::eMailbox;
                break;
            }
        }
    }

    {
        std::uint32_t minImageCount = parameters["frames_in_flight"].getOr<std::uint32_t>(2u);
        vk::SwapchainCreateInfoKHR swapchainCreateInfo;
        swapchainCreateInfo.setSurface(**mSurface)
            .setMinImageCount(minImageCount)
            .setImageFormat(surfaceColorFormat)
            .setImageColorSpace(surfaceColorSpace)
            .setImageExtent(surfaceSize)
            .setImageArrayLayers(1)
            .setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
            .setImageSharingMode(vk::SharingMode::eExclusive)
            .setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity)
            .setCompositeAlpha(vk::CompositeAlphaFlagBitsKHR::eOpaque)
            .setPresentMode(presentMode);

        std::vector<std::uint32_t> queueFamilyIndices;
        if (!mPresentQueueIsSameToGraphicsQueue)
        {
            queueFamilyIndices = {graphicsProvider->getGraphicsQueue().familyIndex,
                                  mPresentQueueFamilyIndex};
            // If the Graphics and present queues are from different queue
            // families, we either have to explicitly transfer ownership of
            // images between the queues, or we have to create the swapchain
            // with imageSharingMode as vk::SharingMode::eConcurrent
            swapchainCreateInfo.setImageSharingMode(vk::SharingMode::eConcurrent)
                .setQueueFamilyIndices(queueFamilyIndices);
        }

        mSwapchain = vk::raii::SwapchainKHR{mGraphicsProvider->getDevice(), swapchainCreateInfo};
    }

    auto images = mSwapchain.getImages();

    {
        vk::ComponentMapping componentMapping{vk::ComponentSwizzle::eR,
                                              vk::ComponentSwizzle::eG,
                                              vk::ComponentSwizzle::eB,
                                              vk::ComponentSwizzle::eA};

        mImageViews.reserve(images.size());
        for (auto& image : images)
        {
            vk::ImageViewCreateInfo createInfo;
            createInfo.setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(mSurfaceFormat.format)
                .setComponents(componentMapping)
                .setSubresourceRange(vk::ImageSubresourceRange{}
                                         .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                         .setBaseMipLevel(0)
                                         .setLevelCount(1)
                                         .setBaseArrayLayer(0)
                                         .setLayerCount(1));

            mImageViews.emplace_back(
                std::make_shared<vk::raii::ImageView>(mGraphicsProvider->getDevice(), createInfo));

            mSwapchainImages.emplace_back(image);
        }
    }

    {
        mSwapchainImageSyncData.reserve(images.size());
        for (std::size_t i = 0; i < images.size(); ++i)
        {
            mSwapchainImageSyncData.emplace_back(*mGraphicsProvider);
        }
    }
}

const vk::Image&
Swapchain::getImage() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    return mSwapchainImages[mCurrentSwapchainImageIndex.value()];
}

const std::shared_ptr<vk::raii::ImageView>&
Swapchain::getImageView() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    return mImageViews[mCurrentSwapchainImageIndex.value()];
}

vk::Result
Swapchain::acquireNextImage()
{
    VOG_ASSERT_MSG(
        !mCurrentSwapchainImageIndex,
        "Previous swapchain image should have been drained before calling this function.");

    mSwapchainImageSyncIndex = (mSwapchainImageSyncIndex + 1u) % mSwapchainImageSyncData.size();
    auto& syncData           = mSwapchainImageSyncData[mSwapchainImageSyncIndex];

    auto [result, index] = mSwapchain.acquireNextImage(gAcquireTimeout, {}, *syncData.fence);

    {
        const vk::Result waitResult = mGraphicsProvider->getDevice().waitForFences(
            {*syncData.fence}, vk::Bool32{1}, std::numeric_limits<std::uint64_t>::max());
        VOG_ASSERT_MSG(waitResult == vk::Result::eSuccess, "Should always be success here");

        mGraphicsProvider->getDevice().resetFences({*syncData.fence});
    }

    VOG_ASSERT_MSG((result == vk::Result::eSuccess) || (result == vk::Result::eSuboptimalKHR),
                   "Should only be success of suboptimal for a case of swapchain resize.");

    mCurrentSwapchainImageIndex.emplace(index);

    return result;
}

vk::Result
Swapchain::present(const vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores)
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 0u, .swapchainCount = 1, .pSwapchains = &*mSwapchain};
    presentInfoKHR.setImageIndices(mCurrentSwapchainImageIndex.value());
    presentInfoKHR.setWaitSemaphores(waitSemaphores);

    auto result = mPresentQueue.presentKHR(presentInfoKHR);

    VOG_ASSERT_MSG(successAcquirePresentResult(result), "Unexpected result code");

    // Reset the swapchain image index to mark it as lost
    mCurrentSwapchainImageIndex.reset();

    return result;
}
} // namespace VOG::Graphics::Vulkan
