#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(SwapchainGeneration);

/** All per-image resources share the same index: the value returned by vkAcquireNextImageKHR. */
struct SwapchainImage
{
    vk::Image           image;
    vk::raii::ImageView imageView;
    /** PE signals this after finishing display of the image (PE→GPU "present done" signal).
     *  Populated via the swap trick in Swapchain::acquireNextImage(). Submit waits this. */
    SemaphorePtr imageAvailableSemaphore;
};

/**
 * Everything one vkCreateSwapchainKHR produced. Swapchain::recreate() makes a new generation and
 * lets the old one go: the old generation dies by reference count, once the last presentation
 * fence of its swapchain signalled and the last command buffer that recorded one of its images
 * was reset. Nothing waits for the device to be idle.
 *
 * Members are destroyed in reverse order: the image views go first, then the swapchain, then the
 * surface it was created for, then the device.
 *
 * @note Render thread only: the swapchain is externally synchronized.
 */
class SwapchainGeneration
{
public:
    SwapchainGeneration(DevicePtr                             _device,
                        std::shared_ptr<vk::raii::SurfaceKHR> _surface,
                        vk::raii::SwapchainKHR                _swapchain,
                        vk::Extent3D                          _extent)
        : device{std::move(_device)}
        , surface{std::move(_surface)}
        , swapchain{std::move(_swapchain)}
        , extent{_extent}
    {
    }

    SwapchainGeneration(const SwapchainGeneration&) = delete;
    SwapchainGeneration(SwapchainGeneration&&)      = delete;

    const DevicePtr                             device;
    const std::shared_ptr<vk::raii::SurfaceKHR> surface;
    vk::raii::SwapchainKHR                      swapchain;
    const vk::Extent3D                          extent;
    std::vector<SwapchainImage>                 images;

    /** Always unsignaled between acquires; rotates through the images slots via the swap trick in
     *  Swapchain::acquireNextImage(). */
    SemaphorePtr spareAcquireSemaphore;
};
} // namespace VOG::Graphics::Vulkan
