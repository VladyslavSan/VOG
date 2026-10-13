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

/** The surface's size, or @p fallback clamped to the supported range if the surface has none. */
vk::Extent2D
chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D fallback)
{
    constexpr std::uint32_t kAppChooses = std::numeric_limits<std::uint32_t>::max();
    if (capabilities.currentExtent.width != kAppChooses &&
        capabilities.currentExtent.height != kAppChooses)
    {
        return capabilities.currentExtent;
    }

    if (fallback.width == 0u || fallback.height == 0u)
    {
        throw std::runtime_error("Surface creation failed. Surface extents are invalid.");
    }

    return {.width  = std::clamp(fallback.width,
                                 capabilities.minImageExtent.width,
                                 capabilities.maxImageExtent.width),
            .height = std::clamp(fallback.height,
                                 capabilities.minImageExtent.height,
                                 capabilities.maxImageExtent.height)};
}
} // namespace

Swapchain::~Swapchain() {}

Swapchain::Swapchain(DevicePtr device, const SwapchainParameters& parameters)
    : mDevice{std::move(device)}
    , mSurface{CreateRenderSurface(*mDevice->instance, parameters.surface, true)}
    , mPresentQueueFamilyIndex{findPresentQueueFamilyIndex(*mDevice, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         mDevice->queueInfos.graphics.familyIndex}
    , mPresentQueue{mDevice->raii(), mPresentQueueFamilyIndex, 0}
    , mSurfaceFormat{mDevice->getSurfaceFormatsKHR(**mSurface).at(0u)}
{
    auto               surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    const vk::Extent2D extent = chooseExtent(surfaceCapabilities, parameters.fallbackExtent);

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

    mGeneration = makeGeneration(nullptr, extent);
}

SwapchainGenerationPtr
Swapchain::makeGeneration(vk::SwapchainKHR oldSwapchain, vk::Extent2D extent)
{
    const std::array                 queueFamilyIndices = {mDevice->queueInfos.graphics.familyIndex,
                                                           mPresentQueueFamilyIndex};
    const vk::SwapchainCreateInfoKHR swapchainCreateInfo = {
        .surface               = **mSurface,
        .minImageCount         = mMinImageCount,
        .imageFormat           = mSurfaceFormat.format,
        .imageColorSpace       = mSurfaceFormat.colorSpace,
        .imageExtent           = extent,
        .imageArrayLayers      = 1u,
        .imageUsage            = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode      = mPresentQueueIsSameToGraphicsQueue ? vk::SharingMode::eExclusive
                                                                    : vk::SharingMode::eConcurrent,
        .queueFamilyIndexCount = mPresentQueueIsSameToGraphicsQueue ? 0u : 2u,
        .pQueueFamilyIndices   = queueFamilyIndices.data(),
        .preTransform          = vk::SurfaceTransformFlagBitsKHR::eIdentity,
        .compositeAlpha        = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode           = mPresentMode,
        .oldSwapchain          = oldSwapchain,
    };

    auto generation = std::make_shared<SwapchainGeneration>(
        mDevice,
        mSurface,
        vk::raii::SwapchainKHR{mDevice->raii(), swapchainCreateInfo},
        vk::Extent3D{.width = extent.width, .height = extent.height, .depth = 1u});

    const auto rawImages = generation->swapchain.getImages();
    generation->images.reserve(rawImages.size());
    for (const vk::Image image : rawImages)
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
        generation->images.push_back({
            .image                   = image,
            .imageView               = vk::raii::ImageView{mDevice->raii(), createInfo},
            .imageAvailableSemaphore = mDevice->createSemaphore(),
        });
    }

    generation->spareAcquireSemaphore = mDevice->createSemaphore();

    return generation;
}

bool
Swapchain::recreate()
{
    const auto         surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    const vk::Extent2D extent =
        chooseExtent(surfaceCapabilities,
                     {.width = mGeneration->extent.width, .height = mGeneration->extent.height});

    if (extent.width == 0u || extent.height == 0u)
    {
        // Surface is currently zero-sized (e.g. minimized); nothing to build.
        return false;
    }

    // The old generation is not waited for. The presentations and command buffers that still use
    // it keep it alive, and it is destroyed when the last of them lets go. Assigning after the
    // build keeps the old generation in place if the build throws.
    mGeneration = makeGeneration(*mGeneration->swapchain, extent);

    return true;
}

std::size_t
Swapchain::getImageCount() const
{
    return mGeneration->images.size();
}

vk::Format
Swapchain::getFormat() const
{
    return mSurfaceFormat.format;
}

Swapchain::ImageAcquireResult
Swapchain::acquireNextImage()
{
    SwapchainGeneration& generation = *mGeneration;

    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS makes eErrorOutOfDateKHR a returned result
    // instead of a thrown vk::OutOfDateKHRError.
    auto [result, index] = generation.swapchain.acquireNextImage(
        gAcquireTimeout, **generation.spareAcquireSemaphore, {});

    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        return {.status                  = AcquireStatus::eOutOfDate,
                .acquired                = nullptr,
                .imageAvailableSemaphore = nullptr};
    }

    if ((result == vk::Result::eTimeout) || (result == vk::Result::eNotReady))
    {
        return {.status                  = AcquireStatus::eSkip,
                .acquired                = nullptr,
                .imageAvailableSemaphore = nullptr};
    }

    // eSuccess or eSuboptimalKHR: swap the spare with images[index].imageAvailableSemaphore.
    // After the swap:
    //   images[index].imageAvailableSemaphore — freshly signaled by the PE; submit waits this.
    //   spareAcquireSemaphore — the evicted slot semaphore, provably unsignaled: re-acquiring
    //     image K proves the full prior cycle (acquire→submit wait→render→present→PE display)
    //     completed, so the prior submit already consumed its wait on this semaphore.
    std::swap(generation.spareAcquireSemaphore, generation.images[index].imageAvailableSemaphore);

    return {.status                  = AcquireStatus::eReady,
            .acquired                = AcquiredSwapchainImagePtr{new AcquiredSwapchainImage{
                mGeneration, index, mSurfaceFormat.format}},
            .imageAvailableSemaphore = generation.images[index].imageAvailableSemaphore};
}

Swapchain::PresentResult
Swapchain::present(const AcquiredSwapchainImagePtr& acquired,
                   SemaphorePtr                     waitSemaphore,
                   const FencePoolPtr&              fencePool)
{
    VOG_ASSERT_MSG(acquired,
                   "Swapchain::present needs the image Swapchain::acquireNextImage gave.");
    VOG_ASSERT_MSG(waitSemaphore, "Swapchain::present needs a semaphore to wait.");

    // The image may come from a generation recreate() has already replaced: it is presented to
    // the swapchain it was acquired from.
    SwapchainGenerationPtr generation = acquired->mGeneration;
    auto                   fence      = fencePool->getShared();

    const std::uint32_t                    imageIndex      = acquired->mImageIndex;
    const vk::Semaphore                    waitHandle      = **waitSemaphore;
    const vk::SwapchainKHR                 swapchainHandle = *generation->swapchain;
    const vk::Fence                        fenceHandle     = fence->handle();
    const vk::SwapchainPresentFenceInfoKHR fenceInfo{
        .swapchainCount = 1u,
        .pFences        = &fenceHandle,
    };
    const vk::PresentInfoKHR presentInfoKHR{
        .pNext              = &fenceInfo,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores    = &waitHandle,
        .swapchainCount     = 1u,
        .pSwapchains        = &swapchainHandle,
        .pImageIndices      = &imageIndex,
    };

    // Only a fence that was handed to the GPU will signal; marking it earlier would hang its
    // destructor if the present threw. The present releases the acquisition and its queue
    // operations count as enqueued, so their fence signals, also when the present reports
    // eErrorOutOfDateKHR or throws SurfaceLostKHRError. Other errors (out of memory, device lost)
    // enqueue nothing and leave the fence and the acquisition alone.
    const auto settle = [&]
    {
        fence->markUsed();
        acquired->markPresented();
    };

    vk::Result result = vk::Result::eSuccess;
    try
    {
        // eSuboptimalKHR and eErrorOutOfDateKHR (a valid result here, see
        // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS) both mean the swapchain needs
        // recreation.
        result = mPresentQueue.presentKHR(presentInfoKHR);
    }
    catch (const vk::SurfaceLostKHRError&)
    {
        settle();
        throw;
    }
    settle();

    const PresentStatus status =
        (result == vk::Result::eSuccess) ? PresentStatus::eReady : PresentStatus::eOutOfDate;

    return {.status       = status,
            .presentation = {.generation    = std::move(generation),
                             .waitSemaphore = std::move(waitSemaphore),
                             .fence         = std::move(fence)}};
}
} // namespace VOG::Graphics::Vulkan
