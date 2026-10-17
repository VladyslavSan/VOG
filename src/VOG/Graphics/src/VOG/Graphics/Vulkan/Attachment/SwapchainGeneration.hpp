#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <type_traits>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(SwapchainGeneration);

/**
 * Everything one vkCreateSwapchainKHR produced. Swapchain::recreate() makes a new generation and
 * lets the old one go: the old generation dies by reference count, once the last presentation
 * fence of its swapchain signalled and the last command buffer that recorded one of its images
 * was reset. Nothing waits for the device to be idle.
 *
 * Private to the Graphics library: the public headers only name SwapchainGeneration through its
 * pointer. It is built in one step and constant afterwards.
 *
 * Members are destroyed in reverse order: the image views, the swapchain, the surface it was
 * created for, then the device.
 *
 * @note Render thread only: the swapchain is externally synchronized.
 */
class SwapchainGeneration
{
public:
    /** All per-image resources share the same index: the value returned by vkAcquireNextImageKHR.
     */
    struct Image
    {
        vk::Image           image;
        vk::raii::ImageView view;
    };

    /** Creates the swapchain from @p createInfo and its image views. */
    SwapchainGeneration(DevicePtr                             device,
                        std::shared_ptr<vk::raii::SurfaceKHR> surface,
                        const vk::SwapchainCreateInfoKHR&     createInfo);

    SwapchainGeneration(const SwapchainGeneration&) = delete;
    SwapchainGeneration(SwapchainGeneration&&)      = delete;

    const DevicePtr                             device;
    const std::shared_ptr<vk::raii::SurfaceKHR> surface;
    const vk::Format                            format;
    const vk::Extent2D                          extent;
    const vk::raii::SwapchainKHR                swapchain;
    const std::vector<Image>                    images;
};

// The images never change after construction, so references into them stay valid.
static_assert(std::is_const_v<decltype(SwapchainGeneration::images)>);
} // namespace VOG::Graphics::Vulkan
