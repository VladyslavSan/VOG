#include "VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <algorithm>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{
std::vector<SwapchainGeneration::Image>
makeImages(const Device& device, const vk::raii::SwapchainKHR& swapchain, vk::Format format)
{
    const std::vector<vk::Image> rawImages = swapchain.getImages();

    std::vector<SwapchainGeneration::Image> images;
    images.reserve(rawImages.size());
    for (const vk::Image image : rawImages)
    {
        const vk::ImageViewCreateInfo createInfo = {
            .image            = image,
            .viewType         = vk::ImageViewType::e2D,
            .format           = format,
            .components       = {.r = vk::ComponentSwizzle::eR,
                                 .g = vk::ComponentSwizzle::eG,
                                 .b = vk::ComponentSwizzle::eB,
                                 .a = vk::ComponentSwizzle::eA},
            .subresourceRange = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                 .baseMipLevel   = 0u,
                                 .levelCount     = 1u,
                                 .baseArrayLayer = 0u,
                                 .layerCount     = 1u}};
        images.push_back({.image = image, .view = vk::raii::ImageView{device.raii(), createInfo}});
    }

    return images;
}
} // namespace

SwapchainGeneration::SwapchainGeneration(DevicePtr                             _device,
                                         std::shared_ptr<vk::raii::SurfaceKHR> _surface,
                                         const vk::SwapchainCreateInfoKHR&     createInfo)
    : device{std::move(_device)}
    , surface{std::move(_surface)}
    , format{createInfo.imageFormat}
    , extent{createInfo.imageExtent}
    , swapchain{device->raii(), createInfo}
    , images{makeImages(*device, swapchain, format)}
{
    // One per image plus the spare.
    mAcquireSemaphores.reserve(images.size() + 1u);
    for (std::size_t i = 0u; i <= images.size(); ++i)
    {
        mAcquireSemaphores.push_back(device->createSemaphore());
    }
}

SwapchainGeneration::Acquire
SwapchainGeneration::acquireNextImage(std::chrono::nanoseconds timeout)
{
    const auto nanoseconds = static_cast<std::uint64_t>(std::max(timeout.count(), std::int64_t{0}));

    // VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS makes eErrorOutOfDateKHR a returned result
    // instead of a thrown vk::OutOfDateKHRError.
    const auto [result, index] =
        swapchain.acquireNextImage(nanoseconds, **mAcquireSemaphores.back(), {});

    if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
    {
        return {.result = result, .index = 0u, .imageAvailable = nullptr};
    }

    // The spare was signaled by the acquire; it becomes the semaphore of that image, and the
    // evicted one is the new spare.
    std::swap(mAcquireSemaphores.back(), mAcquireSemaphores.at(index));

    return {.result = result, .index = index, .imageAvailable = mAcquireSemaphores.at(index)};
}
} // namespace VOG::Graphics::Vulkan
