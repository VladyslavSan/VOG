#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <chrono>
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
 * pointer. It is built in one step and constant afterwards, except for the acquire semaphores,
 * which acquireNextImage() rotates.
 *
 * Members are destroyed in reverse order: the acquire semaphores, the image views, the swapchain,
 * the surface it was created for, then the device.
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

    struct Acquire
    {
        vk::Result result;
        /** Valid when result is eSuccess or eSuboptimalKHR. */
        std::uint32_t index;
        /** Signaled by the presentation engine once the image is free; the submit that renders it
         *  waits this. Valid with the index. */
        SemaphorePtr imageAvailable;
    };

    /** Creates the swapchain from @p createInfo, its image views and its acquire semaphores. */
    SwapchainGeneration(DevicePtr                             device,
                        std::shared_ptr<vk::raii::SurfaceKHR> surface,
                        const vk::SwapchainCreateInfoKHR&     createInfo);

    SwapchainGeneration(const SwapchainGeneration&) = delete;
    SwapchainGeneration(SwapchainGeneration&&)      = delete;

    /**
     * Acquires the next image and hands out its semaphore. This is the only mutator.
     *
     * @param timeout  Negative values count as zero.
     */
    Acquire acquireNextImage(std::chrono::nanoseconds timeout);

    const DevicePtr                             device;
    const std::shared_ptr<vk::raii::SurfaceKHR> surface;
    const vk::Format                            format;
    const vk::Extent2D                          extent;
    const vk::raii::SwapchainKHR                swapchain;
    const std::vector<Image>                    images;

private:
    /**
     * One semaphore per image plus a spare, the last one. The swap trick (SwapchainSync.md): the
     * spare, always unsignaled, goes to vkAcquireNextImageKHR before the index is known; then it
     * is swapped with the semaphore at the returned index. The evicted semaphore is provably
     * unsignaled: re-acquiring image K proves the full prior cycle completed, so the prior submit
     * already consumed its wait on it. That holds only if every acquired image is presented; see
     * the known gap in SwapchainSync.md.
     */
    std::vector<SemaphorePtr> mAcquireSemaphores;
};

// The images never change after construction, so references into them stay valid.
static_assert(std::is_const_v<decltype(SwapchainGeneration::images)>);
} // namespace VOG::Graphics::Vulkan
