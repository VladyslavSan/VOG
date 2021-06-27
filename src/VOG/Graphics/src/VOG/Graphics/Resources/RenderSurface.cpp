#include "VOG/Graphics/Resources/RenderSurface.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/CreateRenderSurface.hpp>

#include <functional>
#include <stdexcept>

namespace VOG::Graphics::Resources
{
namespace
{
std::shared_ptr<vk::raii::ImageView> NullImage = nullptr;
std::uint64_t AcquireTimeout = 10000;

std::uint32_t
FindPresentQueueFamilyIndex(const Api::GraphicsProvider& graphicsProvider,
                            const vk::raii::SurfaceKHR& surface)
{
    const auto& familyInfos = graphicsProvider.GetQueueInfos();
    if (graphicsProvider.GetPhysicalDevice().getSurfaceSupportKHR(familyInfos.graphics.familyIndex,
                                                                  *surface))
        return familyInfos.graphics.familyIndex;

    auto queueFamiliesProperty = graphicsProvider.GetPhysicalDevice().getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamiliesProperty.size(); ++i)
    {
        if (graphicsProvider.GetPhysicalDevice().getSurfaceSupportKHR(i, *surface))
            return i;
    }

    throw std::runtime_error("No present queue found.");

    return std::numeric_limits<std::uint32_t>::max();
}

bool
QueueFamilySupportsPresent(const vk::raii::PhysicalDevice& device, std::uint32_t queueFamilyIndex,
                           const vk::raii::SurfaceKHR& surface)
{
    return device.getSurfaceSupportKHR(queueFamilyIndex, *surface);
}

std::shared_ptr<vk::raii::Queue>
CreatePresentQueue(const vk::raii::PhysicalDevice& physicalDevice, const vk::raii::Device& device,
                   const vk::raii::SurfaceKHR& surface)
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
} // namespace

RenderSurface::~RenderSurface() {}

RenderSurface::RenderSurface(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
                             const Common::JSONContainer& parameters)
    : Attachment{AttachmentUsage::Color, UnknownFormat, AttachmentExtent{}, SampleCount::e1}
    , mGraphicsProvider{graphicsProvider}
    , mSurface{helper::CreateRenderSurface(mGraphicsProvider, parameters, true)}
    , mPresentQueueFamilyIndex{FindPresentQueueFamilyIndex(*mGraphicsProvider, *mSurface)}
    , mPresentQueueIsSameToGraphicsQueue{mPresentQueueFamilyIndex ==
                                         mGraphicsProvider->GetQueueInfos().graphics.familyIndex}
    , mPresentQueue{mGraphicsProvider->GetDevice(), mPresentQueueFamilyIndex, 0}
    , mCurrentSyncronisationIndex{0}
    , mSwapchain{mGraphicsProvider->GetDevice(), vk::SwapchainKHR{}}
{
    auto surfaceFormats = mGraphicsProvider->GetPhysicalDevice().getSurfaceFormatsKHR(**mSurface);
    auto surfaceColorFormat = vk::Format::eUndefined;
    vk::ColorSpaceKHR surfaceColorSpace;
    if (surfaceFormats.empty())
        throw std::runtime_error("Surface creation failed. Could not retrieve surface formats.");
    else
    {
        mSurfaceFormat = surfaceFormats[0];
        surfaceColorFormat = surfaceFormats[0].format;

        m_format = static_cast<AttachmentFormat>(mSurfaceFormat.format);
    }
    surfaceColorSpace = surfaceFormats[0].colorSpace;

    auto surfaceCapabilities =
        mGraphicsProvider->GetPhysicalDevice().getSurfaceCapabilitiesKHR(**mSurface);
    auto surfacePresentModes =
        mGraphicsProvider->GetPhysicalDevice().getSurfacePresentModesKHR(**mSurface);

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

    m_extent.width = surfaceSize.width;
    m_extent.height = surfaceSize.height;
    m_extent.depth = 1;

    auto presentMode = vk::PresentModeKHR::eImmediate;
    for (auto& pm : surfacePresentModes)
    {
        if (pm == vk::PresentModeKHR::eMailbox)
        {
            presentMode = vk::PresentModeKHR::eMailbox;
            break;
        }
    }

    {
        std::uint32_t minImageCount = static_cast<std::uint32_t>(
            parameters["frames_in_flight"]->GetValueOr<Common::JSONContainer::UnsignedInt>(2));
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
            queueFamilyIndices = {mGraphicsProvider->GetQueueInfos().graphics.familyIndex,
                                  mPresentQueueFamilyIndex};
            // If the Graphics and present queues are from different queue families, we either have
            // to explicitly transfer ownership of images between the queues, or we have to create
            // the swapchain with imageSharingMode as vk::SharingMode::eConcurrent
            swapchainCreateInfo.setImageSharingMode(vk::SharingMode::eConcurrent)
                .setQueueFamilyIndices(queueFamilyIndices);
        }

        mSwapchain = vk::raii::SwapchainKHR{mGraphicsProvider->GetDevice(), swapchainCreateInfo};
    }

    auto images = mSwapchain.getImages();

    {
        vk::ComponentMapping componentMapping{vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
                                              vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA};

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
                std::make_shared<vk::raii::ImageView>(mGraphicsProvider->GetDevice(), createInfo));
        }
    }

    mImageReadySemaphore.reserve(mImageViews.size());
    mImageReadyFence.reserve(mImageViews.size());
    for (std::size_t i = 0; i < mImageViews.size(); ++i)
    {
        mImageReadySemaphore.emplace_back(mGraphicsProvider->GetDevice(),
                                          vk::SemaphoreCreateInfo{});
        mImageReadyFence.emplace_back(mGraphicsProvider->GetDevice(), vk::FenceCreateInfo{});
    }
}

const std::shared_ptr<vk::raii::ImageView>&
RenderSurface::GetImageView() const
{
    if (!mCurrentSwapchainImageIndex)
        return NullImage;
    else
        return mImageViews[mCurrentSwapchainImageIndex.value()];
}

bool
RenderSurface::AcquireNextImage()
{
    mCurrentSyncronisationIndex = (mCurrentSyncronisationIndex + 1) % mImageViews.size();

    auto& fence = mImageReadyFence[mCurrentSyncronisationIndex];
    auto& semaphore = mImageReadySemaphore[mCurrentSyncronisationIndex];
    vk::Result waitResult = mGraphicsProvider->GetDevice().waitForFences({*fence}, true, 1000);
    mGraphicsProvider->GetDevice().resetFences({*fence});

    auto [result, index] = mSwapchain.acquireNextImage(AcquireTimeout, *semaphore, *fence);
    if (result != vk::Result::eSuccess)
        return false;

    mCurrentSwapchainImageIndex.emplace(index);
    return true;
}

std::optional<std::reference_wrapper<const vk::raii::Semaphore>>
RenderSurface::GetCurrentImageReadySemaphore() const
{
    if (!mCurrentSwapchainImageIndex)
        return {};

    return std::ref(mImageReadySemaphore[mCurrentSyncronisationIndex]);
}

std::optional<std::reference_wrapper<const vk::raii::Fence>>
RenderSurface::GetCurrentImageReadyFence() const
{
    if (!mCurrentSwapchainImageIndex)
        return {};

    return std::ref(mImageReadyFence[mCurrentSyncronisationIndex]);
}

bool
RenderSurface::Present(std::optional<std::reference_wrapper<const vk::raii::Semaphore>> semaphore)
{
    if (!mCurrentSwapchainImageIndex)
        return false;
    std::array<vk::Semaphore, 2> semaphores = {*mImageReadySemaphore[mCurrentSyncronisationIndex],
                                               semaphore.has_value() ? *semaphore->get()
                                                                     : vk::Semaphore{}};
    vk::PresentInfoKHR presentInfoKHR(semaphores, *mSwapchain, mCurrentSwapchainImageIndex.value());
    presentInfoKHR.setWaitSemaphoreCount(semaphore.has_value() ? 2 : 1);

    auto result = mPresentQueue.presentKHR(presentInfoKHR);
    mCurrentSwapchainImageIndex.reset();
    switch (result)
    {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
        break;
    default:
        return false;
    }

    return true;
}
} // namespace VOG::Graphics::Resources
