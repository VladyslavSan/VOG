#include "VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

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
}
} // namespace VOG::Graphics::Vulkan
