#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Swapchain final : public AttachmentInterface
{
public:
    enum class AcquireStatus : std::uint8_t
    {
        eReady,
        eOutOfDate,
        eSkip
    };

    enum class PresentStatus : std::uint8_t
    {
        eReady,
        eOutOfDate
    };

    struct SwapchainParameters
    {
        std::uint8_t framesInFlight = 1u;

        std::vector<vk::PresentModeKHR> preferredPresentationModes = {
            vk::PresentModeKHR::eMailbox,
            vk::PresentModeKHR::eFifo,
            vk::PresentModeKHR::eFifoRelaxed};

        Common::SurfaceHandle surface;
    };

    struct SwapchainImage
    {
        vk::Image                            image;
        std::shared_ptr<vk::raii::ImageView> imageView;
        vk::raii::Semaphore                  imageAvailableSemaphore;
    };

    struct ImageAcquireResult
    {
        AcquireStatus             status;
        AcquiredSwapchainImagePtr acquired;
    };

    ~Swapchain() override;

private:
    friend class Device;

    Swapchain(DevicePtr device, const SwapchainParameters& parameters);

    void build(vk::SwapchainKHR oldSwapchain);

public:
    ImageAcquireResult acquireNextImage();

    PresentStatus present(vk::Semaphore renderFinishedSemaphore);

    bool recreate();

    std::size_t getImageCount() const;

    /** @deprecated Prefer AcquiredSwapchainImage from acquireNextImage(). */
    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

protected:
    DevicePtr                             mDevice;
    std::shared_ptr<vk::raii::SurfaceKHR> mSurface;

    const std::uint32_t mPresentQueueFamilyIndex;
    const bool          mPresentQueueIsSameToGraphicsQueue;
    vk::raii::Queue     mPresentQueue;

    vk::SurfaceFormatKHR mSurfaceFormat;

    std::uint32_t      mMinImageCount = 1u;
    vk::PresentModeKHR mPresentMode   = vk::PresentModeKHR::eFifo;

    vk::raii::SwapchainKHR      mSwapchain;
    std::vector<SwapchainImage> mImages;

    vk::raii::Semaphore          mSpareAcquireSemaphore;
    std::optional<std::uint32_t> mCurrentSwapchainImageIndex;
};
} // namespace VOG::Graphics::Vulkan
