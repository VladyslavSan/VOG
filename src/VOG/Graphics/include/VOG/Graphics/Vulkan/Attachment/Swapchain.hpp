#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

#include <memory>
#include <optional>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Swapchain : public AttachmentInterface
{
    struct SwapchainImageSyncData
    {
        SwapchainImageSyncData(const DevicePtr& device);

        Fence fence;
    };

public:
    ~Swapchain();

    Swapchain(DevicePtr device, const Common::JSONContainer& parameters);

    vk::Result acquireNextImage();

    vk::Result present(vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores);

public: // from AttachmentInterface
    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

protected:
    DevicePtr                             mDevice;
    std::shared_ptr<vk::raii::SurfaceKHR> mSurface;

    const std::uint32_t mPresentQueueFamilyIndex;
    const bool          mPresentQueueIsSameToGraphicsQueue;
    vk::raii::Queue     mPresentQueue;

    vk::SurfaceFormatKHR   mSurfaceFormat;
    vk::raii::SwapchainKHR mSwapchain;
    std::vector<vk::Image> mSwapchainImages;

    std::vector<std::shared_ptr<vk::raii::ImageView>> mImageViews;

    /** Index of the current swapchain image data */
    std::uint32_t mSwapchainImageSyncIndex;

    /** Array of sync elements for each image out of k frames in flight */
    std::vector<SwapchainImageSyncData> mSwapchainImageSyncData;

    std::optional<std::uint32_t> mCurrentSwapchainImageIndex;
};
} // namespace VOG::Graphics::Vulkan
