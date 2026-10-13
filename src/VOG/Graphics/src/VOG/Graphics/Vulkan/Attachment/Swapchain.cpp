#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
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
    , mSpareAcquireSemaphore{nullptr}
    , mSurfaceFormat{mDevice->getSurfaceFormatsKHR(**mSurface).at(0u)}
{
    mFormat = mSurfaceFormat.format;

    auto surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max() &&
        surfaceCapabilities.currentExtent.height != std::numeric_limits<uint32_t>::max())
    {
        mExtent = vk::Extent3D{.width  = surfaceCapabilities.currentExtent.width,
                               .height = surfaceCapabilities.currentExtent.height,
                               .depth  = 1};
    }
    else
    {
        throw std::runtime_error("Surface creation failed. Surface extents are invalid.");
    }

    mMinImageCount =
        std::max<std::uint32_t>(parameters.framesInFlight, surfaceCapabilities.minImageCount);
    // maxImageCount == 0 means no upper limit.
    if (surfaceCapabilities.maxImageCount != 0u)
    {
        mMinImageCount = std::min(mMinImageCount, surfaceCapabilities.maxImageCount);
    }

    auto surfacePresentModes = mDevice->getSurfacePresentModesKHR(**mSurface);
    // eFifo is always supported; never leave an unverified eImmediate default.
    mPresentMode = vk::PresentModeKHR::eFifo;
    for (const vk::PresentModeKHR mode : parameters.preferredPresentationModes)
    {
        if (std::ranges::find(surfacePresentModes, mode) != surfacePresentModes.end())
        {
            mPresentMode = mode;
            break;
        }
    }

    build(nullptr);
}

void
Swapchain::build(vk::SwapchainKHR oldSwapchain)
{
    {
        const std::array queueFamilyIndices = {mDevice->queueInfos.graphics.familyIndex,
                                               mPresentQueueFamilyIndex};
        const vk::SwapchainCreateInfoKHR swapchainCreateInfo = {
            .surface          = **mSurface,
            .minImageCount    = mMinImageCount,
            .imageFormat      = mFormat,
            .imageColorSpace  = mSurfaceFormat.colorSpace,
            .imageExtent      = vk::Extent2D{.width = mExtent.width, .height = mExtent.height},
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

    mImages.clear();

    {
        auto rawImages = mSwapchain.getImages();
        mImages.reserve(rawImages.size());
        for (auto& image : rawImages)
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
            mImages.push_back({
                .image     = image,
                .imageView = std::make_shared<vk::raii::ImageView>(*mDevice, createInfo),
                .imageAvailableSemaphore = vk::raii::Semaphore{*mDevice, vk::SemaphoreCreateInfo{}},
            });
        }
    }

    mSpareAcquireSemaphore = vk::raii::Semaphore{*mDevice, vk::SemaphoreCreateInfo{}};
}

bool
Swapchain::recreate()
{
    const auto surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);

    mExtent = vk::Extent3D{.width  = surfaceCapabilities.currentExtent.width,
                           .height = surfaceCapabilities.currentExtent.height,
                           .depth  = 1};

    if (mExtent.width == 0u || mExtent.height == 0u)
    {
        // Surface is currently zero-sized (e.g. minimized); nothing to build.
        return false;
    }

    // The render thread is the sole submitter, so waiting for idle lets us tear down the old
    // swapchain immediately without tracking per-frame retirement.
    mDevice->waitIdle();

    mCurrentSwapchainImageIndex.reset();

    vk::raii::SwapchainKHR oldSwapchain = std::move(mSwapchain);
    build(*oldSwapchain);

    return true;
}

const vk::Image&
Swapchain::getImage() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return mImages[mCurrentSwapchainImageIndex.value()].image;
}

const std::shared_ptr<vk::raii::ImageView>&
Swapchain::getImageView() const
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return mImages[mCurrentSwapchainImageIndex.value()].imageView;
}

std::size_t
Swapchain::getImageCount() const
{
    return mImages.size();
}

Swapchain::ImageAcquireResult
Swapchain::acquireNextImage()
{
    VOG_ASSERT_MSG(
        !mCurrentSwapchainImageIndex,
        "Previous swapchain image should have been drained before calling this function.");

    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS makes eErrorOutOfDateKHR a returned result
    // instead of a thrown vk::OutOfDateKHRError.
    auto [result, index] =
        mSwapchain.acquireNextImage(gAcquireTimeout, {*mSpareAcquireSemaphore}, {});

    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        return {.status = AcquireStatus::eOutOfDate, .acquired = nullptr};
    }

    if ((result == vk::Result::eTimeout) || (result == vk::Result::eNotReady))
    {
        return {.status = AcquireStatus::eSkip, .acquired = nullptr};
    }

    // eSuccess or eSuboptimalKHR: swap the spare with mImages[index].imageAvailableSemaphore.
    // After the swap:
    //   mImages[index].imageAvailableSemaphore — freshly signaled by the PE; submit waits this.
    //   mSpareAcquireSemaphore — the evicted slot semaphore, provably unsignaled: re-acquiring
    //     image K proves the full prior cycle (acquire→submit wait→render→present→PE display)
    //     completed, so the prior submit already consumed its wait on this semaphore.
    std::swap(mSpareAcquireSemaphore, mImages[index].imageAvailableSemaphore);
    mCurrentSwapchainImageIndex.emplace(index);

    return {.status   = AcquireStatus::eReady,
            .acquired = std::make_shared<AcquiredSwapchainImage>(
                mImages[index].image,
                mImages[index].imageView,
                mSurfaceFormat.format,
                mExtent,
                &mImages[index].imageAvailableSemaphore)};
}

Swapchain::PresentStatus
Swapchain::present(vk::Semaphore renderFinishedSemaphore)
{
    VOG_ASSERT_MSG(
        mCurrentSwapchainImageIndex,
        "Swapchain::acquireNextImage should have been called before calling this function.");

    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const std::uint32_t      imageIndex = mCurrentSwapchainImageIndex.value();
    const vk::PresentInfoKHR presentInfoKHR{
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores    = &renderFinishedSemaphore,
        .swapchainCount     = 1u,
        .pSwapchains        = &*mSwapchain,
        .pImageIndices      = &imageIndex,
    };

    // eSuboptimalKHR and eErrorOutOfDateKHR (a valid result here, see
    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS) both mean the swapchain needs recreation.
    const auto          result = mPresentQueue.presentKHR(presentInfoKHR);
    const PresentStatus status =
        (result == vk::Result::eSuccess) ? PresentStatus::eReady : PresentStatus::eOutOfDate;

    mCurrentSwapchainImageIndex.reset();

    return status;
}
} // namespace VOG::Graphics::Vulkan
