#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Attachment.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <optional>

namespace VOG::Graphics
{
namespace Api
{
class GraphicsProvider;
} // namespace Api
namespace Resources
{
class RenderSurface : public Attachment
{
public:
    ~RenderSurface();

    RenderSurface(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
                  const Common::JSONContainer& parameters);

    const std::shared_ptr<vk::raii::ImageView>& GetImageView() const override;

    bool AcquireNextImage();

    std::optional<std::reference_wrapper<const vk::raii::Semaphore>>
    GetCurrentImageReadySemaphore() const;
    std::optional<std::reference_wrapper<const vk::raii::Fence>> GetCurrentImageReadyFence() const;

    bool Present(std::optional<std::reference_wrapper<const vk::raii::Semaphore>> semaphore);

protected:
    std::shared_ptr<Api::GraphicsProvider> mGraphicsProvider;
    std::shared_ptr<vk::raii::SurfaceKHR> mSurface;

    const std::uint32_t mPresentQueueFamilyIndex;
    const bool mPresentQueueIsSameToGraphicsQueue;
    vk::raii::Queue mPresentQueue;

    vk::SurfaceFormatKHR mSurfaceFormat;
    vk::raii::SwapchainKHR mSwapchain;
    std::vector<std::shared_ptr<vk::raii::ImageView>> mImageViews;

    std::optional<std::uint32_t> mCurrentSwapchainImageIndex;

    std::size_t mCurrentSyncronisationIndex;
    std::vector<vk::raii::Semaphore> mImageReadySemaphore;
    std::vector<vk::raii::Fence> mImageReadyFence;
};
VOG_DECLARE_PTR(RenderSurface);
} // namespace Resources
} // namespace VOG::Graphics
