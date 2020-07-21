#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Resources/Attachment.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <memory>
#include <optional>

namespace vk
{
struct SurfaceFormatKHR;

namespace raii
{
class Image;
class ImageView;
class Queue;
class SwapchainKHR;
class SurfaceKHR;
class Fence;
class Semaphore;
class Queue;
// class SwapchainData;
} // namespace raii
} // namespace vk

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
    std::shared_ptr<Api::GraphicsProvider> m_graphicsProvider;

    std::shared_ptr<vk::raii::SurfaceKHR> m_surface;

    const std::uint32_t m_presentQueueFamilyIndex;
    const bool m_presentQueueIsSameToGraphicsQueue;
    std::shared_ptr<vk::raii::Queue> m_presentQueue;

    std::unique_ptr<vk::SurfaceFormatKHR> m_surfaceFormat;
    std::shared_ptr<vk::raii::SwapchainKHR> m_swapchain;
    std::vector<std::shared_ptr<vk::raii::ImageView>> m_imageViews;

    std::optional<std::uint32_t> m_currentSwapchainImageIndex;

    std::size_t m_currentSyncronisationIndex;
    std::vector<vk::raii::Semaphore> m_imageReadySemaphore;
    std::vector<vk::raii::Fence> m_imageReadyFence;
};
VOG_DECLARE_PTR(RenderSurface);
} // namespace Resources
} // namespace VOG::Graphics
