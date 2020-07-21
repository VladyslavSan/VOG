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
FindPresentQueueFamilyIndex(const Api::GraphicsProvider& GraphicsProvider,
                            const vk::raii::SurfaceKHR& surface)
{
    if (GraphicsProvider.GetPhysicalDevice()->getSurfaceSupportKHR(
            GraphicsProvider.GetGraphicsQueueInfo().first, *surface))
        return GraphicsProvider.GetGraphicsQueueInfo().first;

    auto queueFamiliesProperty = GraphicsProvider.GetPhysicalDevice()->getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamiliesProperty.size(); ++i)
    {
        if (GraphicsProvider.GetPhysicalDevice()->getSurfaceSupportKHR(i, *surface))
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
    , m_graphicsProvider{graphicsProvider}
    , m_surface{helper::CreateRenderSurface(m_graphicsProvider, parameters, true)}
    , m_presentQueueFamilyIndex{FindPresentQueueFamilyIndex(*m_graphicsProvider, *m_surface)}
    , m_presentQueueIsSameToGraphicsQueue{m_presentQueueFamilyIndex ==
                                          m_graphicsProvider->GetGraphicsQueueInfo().first}
    , m_presentQueue{std::make_shared<vk::raii::Queue>(*m_graphicsProvider->GetDevice(),
                                                       m_presentQueueFamilyIndex, 0)}
    , m_currentSyncronisationIndex{0}
{
    auto surfaceFormats =
        m_graphicsProvider->GetPhysicalDevice()->getSurfaceFormatsKHR(**m_surface);
    auto surfaceColorFormat = vk::Format::eUndefined;
    vk::ColorSpaceKHR surfaceColorSpace;
    if (surfaceFormats.empty())
        throw std::runtime_error("Surface creation failed. Could not retrieve surface formats.");
    else
    {
        m_surfaceFormat = std::make_unique<vk::SurfaceFormatKHR>(surfaceFormats[0]);
        surfaceColorFormat = surfaceFormats[0].format;

        m_format = static_cast<AttachmentFormat>(m_surfaceFormat->format);
    }
    surfaceColorSpace = surfaceFormats[0].colorSpace;

    auto surfaceCapabilities =
        m_graphicsProvider->GetPhysicalDevice()->getSurfaceCapabilitiesKHR(**m_surface);
    auto surfacePresentModes =
        m_graphicsProvider->GetPhysicalDevice()->getSurfacePresentModesKHR(**m_surface);

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
        swapchainCreateInfo.setSurface(**m_surface)
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
        if (!m_presentQueueIsSameToGraphicsQueue)
        {
            queueFamilyIndices = {m_graphicsProvider->GetGraphicsQueueInfo().first,
                                  m_presentQueueFamilyIndex};
            // If the Graphics and present queues are from different queue families, we either have
            // to explicitly transfer ownership of images between the queues, or we have to create
            // the swapchain with imageSharingMode as vk::SharingMode::eConcurrent
            swapchainCreateInfo.setImageSharingMode(vk::SharingMode::eConcurrent)
                .setQueueFamilyIndices(queueFamilyIndices);
        }

        m_swapchain = std::make_unique<vk::raii::SwapchainKHR>(*m_graphicsProvider->GetDevice(),
                                                               swapchainCreateInfo);
    }

    auto images = m_swapchain->getImages();

    {
        vk::ComponentMapping componentMapping{vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG,
                                              vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA};

        m_imageViews.reserve(images.size());
        for (auto& image : images)
        {
            vk::ImageViewCreateInfo createInfo;
            createInfo.setImage(image)
                .setViewType(vk::ImageViewType::e2D)
                .setFormat(m_surfaceFormat->format)
                .setComponents(componentMapping)
                .setSubresourceRange(vk::ImageSubresourceRange{}
                                         .setAspectMask(vk::ImageAspectFlagBits::eColor)
                                         .setBaseMipLevel(0)
                                         .setLevelCount(1)
                                         .setBaseArrayLayer(0)
                                         .setLayerCount(1));

            m_imageViews.push_back(std::make_unique<vk::raii::ImageView>(
                *m_graphicsProvider->GetDevice(), createInfo));
        }
    }

    m_imageReadySemaphore.reserve(m_imageViews.size());
    m_imageReadyFence.reserve(m_imageViews.size());
    for (std::size_t i = 0; i < m_imageViews.size(); ++i)
    {
        m_imageReadySemaphore.emplace_back(
            vk::raii::Semaphore{*m_graphicsProvider->GetDevice(), vk::SemaphoreCreateInfo{}});
        m_imageReadyFence.emplace_back(
            vk::raii::Fence{*m_graphicsProvider->GetDevice(), vk::Fence{}});
    }
}

const std::shared_ptr<vk::raii::ImageView>&
RenderSurface::GetImageView() const
{
    if (!m_currentSwapchainImageIndex)
        return NullImage;
    else
        return m_imageViews[m_currentSwapchainImageIndex.value()];
}

bool
RenderSurface::AcquireNextImage()
{
    m_currentSyncronisationIndex = (m_currentSyncronisationIndex + 1) & m_imageViews.size();

    auto [result, index] = m_swapchain->acquireNextImage(
        AcquireTimeout, *m_imageReadySemaphore[m_currentSyncronisationIndex],
        *m_imageReadyFence[m_currentSyncronisationIndex]);
    if (result != vk::Result::eSuccess)
        return false;

    m_currentSwapchainImageIndex.emplace(index);
    return true;
}

std::optional<std::reference_wrapper<const vk::raii::Semaphore>>
RenderSurface::GetCurrentImageReadySemaphore() const
{
    if (!m_currentSwapchainImageIndex)
        return {};

    return std::ref(m_imageReadySemaphore[m_currentSyncronisationIndex]);
}

std::optional<std::reference_wrapper<const vk::raii::Fence>>
RenderSurface::GetCurrentImageReadyFence() const
{
    if (!m_currentSwapchainImageIndex)
        return {};

    return std::ref(m_imageReadyFence[m_currentSyncronisationIndex]);
}

bool
RenderSurface::Present(std::optional<std::reference_wrapper<const vk::raii::Semaphore>> semaphore)
{
    if (!m_currentSwapchainImageIndex)
        return false;
    std::array<vk::Semaphore, 1> semaphores = {semaphore.has_value() ? *semaphore->get()
                                                                     : vk::Semaphore{}};
    vk::PresentInfoKHR presentInfoKHR(semaphores, **m_swapchain,
                                      m_currentSwapchainImageIndex.value());
    auto result = m_presentQueue->presentKHR(presentInfoKHR);
    switch (result)
    {
    case vk::Result::eSuccess:
        break;
    case vk::Result::eSuboptimalKHR:
        break;
    default:
        assert(false);
    }

    return true;
}
} // namespace VOG::Graphics::Resources
