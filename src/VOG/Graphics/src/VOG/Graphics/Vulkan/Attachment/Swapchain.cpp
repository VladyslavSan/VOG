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
findPresentQueueFamilyIndex(const Device& device, const vk::raii::SurfaceKHR& surface)
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
} // namespace

Swapchain::SwapchainImageSyncData::SwapchainImageSyncData(const DevicePtr& device)
    : semaphore{*device, vk::SemaphoreCreateInfo{}}
    , fence{device->createFence()}
{
}

Swapchain::~Swapchain() {}

Swapchain::Swapchain(DevicePtr device, const SwapchainParameters& parameters)
    : AttachmentInterface{AttachmentUsage::eColor, vk::Format::eUndefined, {}, SampleCount::e1}
    , mDevice{std::move(device)}
    , mSurface{CreateRenderSurface(*mDevice->instance, parameters.surface, true)}
    , mPresentQueueFamilyIndex{findPresentQueueFamilyIndex(*mDevice, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         mDevice->queueInfos.graphics.familyIndex}
    , mPresentQueue{*mDevice, mPresentQueueFamilyIndex, 0}
    , mSwapchain{nullptr}
    , mSurfaceFormat{mDevice->getSurfaceFormatsKHR(**mSurface).at(0u)}
{
    mFormat = mSurfaceFormat.format;

    auto surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    auto surfacePresentModes = mDevice->getSurfacePresentModesKHR(**mSurface);

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

    mMinImageCount = parameters.framesInFlight;

    mPresentMode = vk::PresentModeKHR::eImmediate;
    {
        bool foundPresentationMode = false;
        if (!parameters.preferredPresentationModes.empty())
        {
            for (const vk::PresentModeKHR mode : parameters.preferredPresentationModes)
            {
                if (std::ranges::find(surfacePresentModes, mode) != surfacePresentModes.end())
                {
                    mPresentMode          = mode;
                    foundPresentationMode = true;

                    break;
                }
            }
        }

        if (!foundPresentationMode)
        {
            for (auto& spm : surfacePresentModes)
            {
                if (spm == vk::PresentModeKHR::eMailbox)
                {
                    mPresentMode = vk::PresentModeKHR::eMailbox;
                    break;
                }
            }
        }
    }

    build(surfaceSize, nullptr);
}

void
Swapchain::build(vk::Extent2D extent, vk::SwapchainKHR oldSwapchain)
{
    mExtent.width  = extent.width;
    mExtent.height = extent.height;
    mExtent.depth  = 1;

    {
        const std::array queueFamilyIndices = {mDevice->queueInfos.graphics.familyIndex,
                                               mPresentQueueFamilyIndex};
        const vk::SwapchainCreateInfoKHR swapchainCreateInfo = {
            .surface          = **mSurface,
            .minImageCount    = mMinImageCount,
            .imageFormat      = mFormat,
            .imageColorSpace  = mSurfaceFormat.colorSpace,
            .imageExtent      = extent,
            .imageArrayLayers = 1u,
            .imageUsage       = vk::ImageUsageFlagBits::eColorAttachment,
            .imageSharingMode = mPresentQueueIsSameToGraphicsQueue ? vk::SharingMode::eExclusive
                                                                   : vk::SharingMode::eConcurrent,
            .queueFamilyIndexCount = mPresentQueueIsSameToGraphicsQueue ? 0u : 2u,
            .pQueueFamilyIndices   = queueFamilyIndices.data(),
            .preTransform          = vk::SurfaceTransformFlagBitsKHR::eIdentity,
            .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
            .presentMode           = mPresentMode,
            .oldSwapchain          = oldSwapchain,
        };

        mSwapchain = vk::raii::SwapchainKHR{*mDevice, swapchainCreateInfo};
    }

    mSwapchainImages.clear();
    mImageViews.clear();
    mSwapchainImageSyncData.clear();
    mSwapchainImageSyncIndex = 0u;

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

bool
Swapchain::recreate()
{
    const auto         surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    const vk::Extent2D extent              = surfaceCapabilities.currentExtent;

    if (extent.width == 0u || extent.height == 0u)
    {
        // Surface is currently zero-sized (e.g. minimized); nothing to build.
        return false;
    }

    // The render thread is the sole submitter, so waiting for idle lets us tear down the old
    // swapchain immediately without tracking per-frame retirement.
    mDevice->waitIdle();

    mCurrentSwapchainImageIndex.reset();

    vk::raii::SwapchainKHR oldSwapchain = std::move(mSwapchain);
    build(extent, *oldSwapchain);

    return true;
}

const vk::Image&
Swapchain::getImage() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return mSwapchainImages[mCurrentSwapchainImageIndex.value()];
}

const std::shared_ptr<vk::raii::ImageView>&
Swapchain::getImageView() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return mImageViews[mCurrentSwapchainImageIndex.value()];
}

Swapchain::ImageAcquireResult
Swapchain::acquireNextImage()
{
    VOG_ASSERT_MSG(
        !mCurrentSwapchainImageIndex,
        "Previous swapchain image should have been drained before calling this function.");

    mSwapchainImageSyncIndex = (mSwapchainImageSyncIndex + 1u) % mSwapchainImageSyncData.size();
    auto& syncData           = mSwapchainImageSyncData[mSwapchainImageSyncIndex];

    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS makes eErrorOutOfDateKHR a returned result
    // instead of a thrown vk::OutOfDateKHRError.
    auto [result, index] = mSwapchain.acquireNextImage(gAcquireTimeout, {*syncData.semaphore}, {});

    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        return {.status = AcquireStatus::eOutOfDate, .semaphore = nullptr};
    }

    if ((result == vk::Result::eTimeout) || (result == vk::Result::eNotReady))
    {
        // No image became available within the timeout; the semaphore stays unsignaled.
        return {.status = AcquireStatus::eSkip, .semaphore = nullptr};
    }

    // eSuccess or eSuboptimalKHR: the image is acquired and the semaphore is signaled. A
    // suboptimal swapchain is still presentable; present() will trigger recreation afterwards.
    mCurrentSwapchainImageIndex.emplace(index);

    return {.status = AcquireStatus::eReady, .semaphore = &syncData.semaphore};
}

Swapchain::PresentStatus
Swapchain::present(const vk::ArrayProxy<const vk::Semaphore> waitSemaphores)
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = waitSemaphores.size(),
        .pWaitSemaphores    = waitSemaphores.data(),
        .swapchainCount     = 1u,
        .pSwapchains        = &*mSwapchain,
        .pImageIndices =
            &mCurrentSwapchainImageIndex.value(), // NOLINT(bugprone-unchecked-optional-access)
    };

    // eSuboptimalKHR and eErrorOutOfDateKHR (a valid result here, see
    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS) both mean the swapchain needs recreation.
    const auto          result = mPresentQueue.presentKHR(presentInfoKHR);
    const PresentStatus status =
        (result == vk::Result::eSuccess) ? PresentStatus::eReady : PresentStatus::eOutOfDate;

    // Reset the swapchain image index to mark it as lost
    mCurrentSwapchainImageIndex.reset();

    return status;
}
} // namespace VOG::Graphics::Vulkan
