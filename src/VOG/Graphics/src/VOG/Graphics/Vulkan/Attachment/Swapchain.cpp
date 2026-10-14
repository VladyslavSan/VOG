#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/CreateRenderSurface.hpp>
#include <VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/InDestructor.hpp>

#include <algorithm>
#include <exception>
#include <format>
#include <limits>
#include <optional>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
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

/** The surface reports currentExtent as 0xFFFFFFFF when the application chooses the size. */
bool
appChoosesExtent(const vk::SurfaceCapabilitiesKHR& capabilities)
{
    constexpr std::uint32_t kAppChooses = std::numeric_limits<std::uint32_t>::max();

    return capabilities.currentExtent.width == kAppChooses ||
           capabilities.currentExtent.height == kAppChooses;
}

/**
 * The size of the swapchain images: the surface's own, or @p requested clamped to the supported
 * range where the application chooses.
 *
 * @return nothing if the size is zero, from either source (e.g. a minimized window).
 */
std::optional<vk::Extent2D>
chooseExtent(const vk::SurfaceCapabilitiesKHR& capabilities, vk::Extent2D requested)
{
    vk::Extent2D extent = capabilities.currentExtent;
    if (appChoosesExtent(capabilities))
    {
        extent = vk::Extent2D{.width  = std::clamp(requested.width,
                                                   capabilities.minImageExtent.width,
                                                   capabilities.maxImageExtent.width),
                              .height = std::clamp(requested.height,
                                                   capabilities.minImageExtent.height,
                                                   capabilities.maxImageExtent.height)};
        // Clamping would turn a zero request into the minimum extent; zero means "no window".
        if (requested.width == 0u || requested.height == 0u)
        {
            return std::nullopt;
        }
    }

    if (extent.width == 0u || extent.height == 0u)
    {
        return std::nullopt;
    }

    return extent;
}
} // namespace

Swapchain::~Swapchain() = default;

Swapchain::Presentation::Presentation(SwapchainGenerationPtr    generation,
                                      SemaphorePtr              waitSemaphore,
                                      FencePool::FenceHandlePtr fence)
    : mGeneration{std::move(generation)}
    , mWaitSemaphore{std::move(waitSemaphore)}
    , mFence{std::move(fence)}
{
}

Swapchain::Presentation::~Presentation() noexcept
{
    // The moved-from Presentation has no fence. The generation and the semaphore are released
    // after this body, once the presentation engine is done with them.
    if (mFence)
    {
        inDestructor("Waiting for the present fence",
                     [this]
                     {
                         [[maybe_unused]] const vk::Result result =
                             mFence->wait(std::numeric_limits<std::uint64_t>::max());
                     });
    }
}

Swapchain::Swapchain(DevicePtr device, const SwapchainParameters& parameters)
    : mDevice{std::move(device)}
    , mSurface{CreateRenderSurface(*mDevice->instance, parameters.surface, true)}
    , mPresentQueueFamilyIndex{findPresentQueueFamilyIndex(*mDevice, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         mDevice->queueInfos.graphics.familyIndex}
    , mPresentQueue{mDevice->raii(), mPresentQueueFamilyIndex, 0}
    , mSurfaceFormat{mDevice->getSurfaceFormatsKHR(**mSurface).at(0u)}
{
    const auto surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    const auto extent              = chooseExtent(surfaceCapabilities, parameters.extent);
    if (!extent)
    {
        throw std::runtime_error{
            appChoosesExtent(surfaceCapabilities)
                ? std::format("Swapchain: the surface leaves the image size to the application and "
                              "SwapchainParameters::extent is {}x{}.",
                              parameters.extent.width,
                              parameters.extent.height)
                : "Swapchain: the surface is zero-sized."};
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

    mGeneration = makeGeneration(nullptr, *extent);
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

    return std::make_shared<SwapchainGeneration>(mDevice, mSurface, swapchainCreateInfo);
}

bool
Swapchain::recreate(vk::Extent2D requested)
{
    const auto surfaceCapabilities = mDevice->getSurfaceCapabilitiesKHR(**mSurface);
    const auto extent              = chooseExtent(surfaceCapabilities, requested);
    if (!extent)
    {
        // Zero-sized (e.g. minimized); nothing to build.
        return false;
    }

    // The old generation is not waited for. The presentations and command buffers that still use
    // it keep it alive, and it is destroyed when the last of them lets go. Assigning after the
    // build keeps the old generation in place if the build throws.
    mGeneration = makeGeneration(*mGeneration->swapchain, *extent);

    return true;
}

std::size_t
Swapchain::getImageCount() const
{
    return mGeneration->images.size();
}

vk::Extent2D
Swapchain::getExtent() const
{
    return mGeneration->extent;
}

vk::Format
Swapchain::getFormat() const
{
    return mSurfaceFormat.format;
}

Swapchain::ImageAcquireResult
Swapchain::acquireNextImage(std::chrono::nanoseconds timeout)
{
    auto [result, index, imageAvailable] = mGeneration->acquireNextImage(timeout);

    if (result == vk::Result::eErrorOutOfDateKHR)
    {
        return {.status = AcquireStatus::eOutOfDate, .acquired = nullptr};
    }

    if ((result == vk::Result::eTimeout) || (result == vk::Result::eNotReady))
    {
        return {.status = AcquireStatus::eSkip, .acquired = nullptr};
    }

    // eSuccess or eSuboptimalKHR.
    return {.status   = AcquireStatus::eReady,
            .acquired = AcquiredSwapchainImagePtr{
                new AcquiredSwapchainImage{mGeneration, index, std::move(imageAvailable)}}};
}

Swapchain::PresentResult
Swapchain::present(const AcquiredSwapchainImagePtr& acquired,
                   SemaphorePtr                     waitSemaphore,
                   FencePool&                       fencePool)
{
    VOG_ASSERT_MSG(acquired,
                   "Swapchain::present needs the image Swapchain::acquireNextImage gave.");
    VOG_ASSERT_MSG(waitSemaphore, "Swapchain::present needs a semaphore to wait.");
    // Checked before any driver call: presenting an image twice is invalid usage the driver may
    // not survive. No death test covers it, because forking with a live MoltenVK device is unsafe.
    VOG_ASSERT_MSG(!acquired->mPresented, "A swapchain image can be presented once.");

    // The image may come from a generation recreate() has already replaced: it is presented to
    // the swapchain it was acquired from.
    //
    // `generation` is declared before `fence` on purpose. If the surface is lost, present()
    // rethrows below and both unwind; locals are destroyed in reverse order, so the armed fence is
    // destroyed, and waited, before the generation and the semaphore the presentation engine still
    // holds are released.
    SwapchainGenerationPtr    generation = acquired->mGeneration;
    FencePool::FenceHandlePtr fence      = fencePool.take();

    const std::uint32_t    imageIndex      = acquired->mImageIndex;
    const vk::Semaphore    waitHandle      = **waitSemaphore;
    const vk::SwapchainKHR swapchainHandle = *generation->swapchain;

    // The fence is armed when the lambda returns, also when the present reports
    // eErrorOutOfDateKHR or the surface is lost: the present's queue operations count as enqueued,
    // so the presentation engine still waits the semaphore and uses the swapchain, and the fence
    // signals when it is done. Hence the lambda catches SurfaceLostKHRError, lets `fence` arm, and
    // present() rethrows below. Any other error enqueues nothing: it leaves the fence unarmed and
    // the acquisition alone. VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS makes
    // eErrorOutOfDateKHR a returned result.
    vk::Result         result = vk::Result::eSuccess;
    std::exception_ptr lost;
    fence->arm(
        [&](vk::Fence vkFence)
        {
            const vk::SwapchainPresentFenceInfoKHR fenceInfo{
                .swapchainCount = 1u,
                .pFences        = &vkFence,
            };
            const vk::PresentInfoKHR presentInfoKHR{
                .pNext              = &fenceInfo,
                .waitSemaphoreCount = 1u,
                .pWaitSemaphores    = &waitHandle,
                .swapchainCount     = 1u,
                .pSwapchains        = &swapchainHandle,
                .pImageIndices      = &imageIndex,
            };

            try
            {
                result = mPresentQueue.presentKHR(presentInfoKHR);
            }
            catch (const vk::SurfaceLostKHRError&)
            {
                // VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT cannot happen: the extension is
                // never enabled.
                lost = std::current_exception();
            }
        });

    // The present releases the acquisition, also when it reports eErrorOutOfDateKHR.
    acquired->markPresented();
    if (lost)
    {
        std::rethrow_exception(lost);
    }

    // eSuboptimalKHR and eErrorOutOfDateKHR both mean the swapchain needs recreation.
    const PresentStatus status =
        (result == vk::Result::eSuccess) ? PresentStatus::eReady : PresentStatus::eOutOfDate;

    return {.status = status,
            .presentation =
                Presentation{std::move(generation), std::move(waitSemaphore), std::move(fence)}};
}
} // namespace VOG::Graphics::Vulkan
