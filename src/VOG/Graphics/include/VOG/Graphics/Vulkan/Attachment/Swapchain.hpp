#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

#include <memory>
#include <optional>

namespace VOG::Graphics
{
class GraphicsProvider;
}

namespace VOG::Graphics::Vulkan
{
class Swapchain : public AttachmentInterface
{
    struct SwapchainImageSyncData
    {
        SwapchainImageSyncData(const GraphicsProvider&);

        vk::raii::Fence fence;
    };

public:
    ~Swapchain();

    Swapchain(const std::shared_ptr<GraphicsProvider>& graphicsProvider,
              const Common::JSONContainer&             parameters);

    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

    vk::Result acquireNextImage();

    vk::Result present(const vk::ArrayProxyNoTemporaries<const vk::Semaphore> waitSemaphores);

protected:
    std::shared_ptr<GraphicsProvider>     mGraphicsProvider;
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
VOG_DECLARE_PTR(Swapchain);
} // namespace VOG::Graphics::Vulkan
