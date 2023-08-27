#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
constexpr std::uint64_t gAcquireTimeout = 10000u;

std::uint32_t
FindPresentQueueFamilyIndex(const Device& device, const vk::raii::SurfaceKHR& surface)
{
    {

        const std::uint32_t graphicsQueueIndex = device.queueInfos.graphics.familyIndex;
        if (device.getSurfaceSupportKHR(graphicsQueueIndex, *surface) != 0u)
        {
            return graphicsQueueIndex;
        }
    }

    auto queueFamiliesProperty = device.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamiliesProperty.size(); ++i)
    {
        if (device.getSurfaceSupportKHR(i, *surface) != 0u)
        {
            return i;
        }
    }

    throw std::runtime_error("No present queue found.");
}

bool
successAcquirePresentResult(vk::Result result)
{
    return (result == vk::Result::eSuccess) || (result == vk::Result::eSuboptimalKHR);
}
} // namespace

Swapchain::SwapchainImageSyncData::SwapchainImageSyncData(const DevicePtr& device)
    : fence{device}
{
}

Swapchain::~Swapchain() {}

Swapchain::Swapchain(DevicePtr device, const Common::JSONContainer& parameters)
    : AttachmentInterface{AttachmentUsage::eColor, vk::Format::eUndefined, {}, SampleCount::e1}
    , mDevice{std::move(device)}
    , mSurface{CreateRenderSurface(*mDevice->instance, parameters, true)}
    , mPresentQueueFamilyIndex{FindPresentQueueFamilyIndex(*mDevice, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         mDevice->queueInfos.graphics.familyIndex}
    , mPresentQueue{*mDevice, mPresentQueueFamilyIndex, 0}
    , mSwapchain{nullptr}
    , mSurfaceFormat{mDevice->getPhysicalDevice().getSurfaceFormatsKHR(**mSurface).at(0u)}
{
    mFormat = mSurfaceFormat.format;

    auto surfaceCapabilities = mDevice->getPhysicalDevice().getSurfaceCapabilitiesKHR(**mSurface);
    auto surfacePresentModes = mDevice->getPhysicalDevice().getSurfacePresentModesKHR(**mSurface);

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
    const std::string preferredPresentationMode = parameters["present-mode"].getOr<std::string>("");
    if (!preferredPresentationMode.empty())
    {
        spdlog::info("Requested presentation mode : {}.", preferredPresentationMode);
        bool foundPresentMode = false;
        for (const auto& spm : surfacePresentModes)
        {
            if (preferredPresentationMode == vk::to_string(spm))
            {
                foundPresentMode = true;
                presentMode      = spm;
            }
        }
        if (!foundPresentMode)
        {
            spdlog::info("Requested presentation mode was not found.");
        }
    }
    else
    {
        for (auto& spm : surfacePresentModes)
        {
            if (spm == vk::PresentModeKHR::eMailbox)
            {
                presentMode = vk::PresentModeKHR::eMailbox;
                break;
            }
        }
    }

    {
        std::uint32_t minImageCount = parameters["frames_in_flight"].getOr<std::uint32_t>(2u);

        const std::array queueFamilyIndices = {mDevice->queueInfos.graphics.familyIndex,
                                               mPresentQueueFamilyIndex};
        const vk::SwapchainCreateInfoKHR swapchainCreateInfo = {
            .surface          = **mSurface,
            .minImageCount    = minImageCount,
            .imageFormat      = mFormat,
            .imageColorSpace  = mSurfaceFormat.colorSpace,
            .imageExtent      = surfaceSize,
            .imageArrayLayers = 1u,
            .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = mPresentQueueIsSameToGraphicsQueue ? vk::SharingMode::eExclusive
                                                                   : vk::SharingMode::eConcurrent,
            .queueFamilyIndexCount = mPresentQueueIsSameToGraphicsQueue ? 0u : 2u,
            .pQueueFamilyIndices   = queueFamilyIndices.data(),
            .preTransform          = vk::SurfaceTransformFlagBitsKHR::eIdentity,
            .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode           = presentMode,
        };

        mSwapchain = vk::raii::SwapchainKHR{*mDevice, swapchainCreateInfo};
    }

    {
        auto images = mSwapchain.getImages();
        mImageViews.reserve(images.size());
        for (auto& image : images)
        {
            const vk::ImageViewCreateInfo createInfo = {
                .image            = image,
                .viewType         = vk::ImageViewType::e2D,
                .format           = mSurfaceFormat.format,
                .components       = {.r = vk::ComponentSwizzle::eR,
                                     .g = vk::ComponentSwizzle::eG,
                                     .b = vk::ComponentSwizzle::eB,
                                     .a = vk::ComponentSwizzle::eA},
                .subresourceRange = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                     .baseMipLevel   = 0u,
                                     .levelCount     = 1u,
                                     .baseArrayLayer = 0u,
                                     .layerCount     = 1u}};
            mImageViews.emplace_back(std::make_shared<vk::raii::ImageView>(*mDevice, createInfo));
            mSwapchainImages.emplace_back(image);
        }
    }

    {
        mSwapchainImageSyncData.reserve(mImageViews.size());
        for (std::size_t i = 0; i < mImageViews.size(); ++i)
        {
            mSwapchainImageSyncData.emplace_back(mDevice);
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

    auto [result, index] =
        mSwapchain.acquireNextImage(gAcquireTimeout, {}, **syncData.fence.useFence());
    VOG_ASSERT_MSG((result == vk::Result::eSuccess) || (result == vk::Result::eSuboptimalKHR),
                   "Should only be success of suboptimal for a case of swapchain resize.");

    {

        const vk::Result waitResult =
            syncData.fence.wait(std::numeric_limits<std::uint64_t>::max());
        VOG_ASSERT_MSG(waitResult == vk::Result::eSuccess, "Should always be success here");
        syncData.fence.reset();
    }

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
