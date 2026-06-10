#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Fence.hpp>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Swapchain : public AttachmentInterface
{
    struct SwapchainImageSyncData
    {
        SwapchainImageSyncData(const DevicePtr& device);

        vk::raii::Semaphore semaphore;
        Fence               fence;
    };

public:
    struct SwapchainParameters
    {
        /** Frames in flight, might be lower than requested. */
        std::uint8_t framesInFlight = 1u;

        /** Priority list of presentation modes from most preferred to least. */
        std::vector<vk::PresentModeKHR> preferredPresentationModes = {
            vk::PresentModeKHR::eMailbox,
            vk::PresentModeKHR::eFifo,
            vk::PresentModeKHR::eFifoRelaxed};

        Common::SurfaceHandle surface;
    };

    enum class AcquireStatus : std::uint8_t
    {
        /** An image was acquired and is ready to be rendered into. */
        eReady,
        /** The swapchain no longer matches the surface and must be recreated. */
        eOutOfDate,
        /** No image this iteration (timeout / not ready); retry next loop. */
        eSkip
    };

    enum class PresentStatus : std::uint8_t
    {
        /** Presentation succeeded and the swapchain still matches the surface. */
        eReady,
        /** The swapchain no longer matches the surface and must be recreated. */
        eOutOfDate
    };

    struct ImageAcquireResult
    {
        AcquireStatus              status;
        const vk::raii::Semaphore* semaphore; // Valid only when status == eReady.
    };

    ~Swapchain();

private:
    friend class Device;

    Swapchain(DevicePtr device, const SwapchainParameters& parameters);

    /**
     * (Re)creates the swapchain, image views and per-image sync data for the given extent.
     *
     * @param extent        Target image extent (must be non-zero).
     * @param oldSwapchain  Previous swapchain handle for driver resource reuse (may be null).
     */
    void build(vk::Extent2D extent, vk::SwapchainKHR oldSwapchain);

public:
    ImageAcquireResult acquireNextImage();

    PresentStatus present(vk::ArrayProxy<const vk::Semaphore> waitSemaphores);

    /**
     * Recreates the swapchain to match the current surface size. Waits for the device to be idle
     * before tearing down the old swapchain.
     *
     * @return true if the swapchain was rebuilt; false if skipped because the surface is currently
     *         zero-sized (e.g. the window is minimized).
     */
    bool recreate();

    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

protected:
    DevicePtr                             mDevice;
    std::shared_ptr<vk::raii::SurfaceKHR> mSurface;

    const std::uint32_t mPresentQueueFamilyIndex;
    const bool          mPresentQueueIsSameToGraphicsQueue;
    vk::raii::Queue     mPresentQueue;

    vk::SurfaceFormatKHR mSurfaceFormat;

    /** Selected once at construction and reused on every rebuild. */
    std::uint32_t      mMinImageCount = 1u;
    vk::PresentModeKHR mPresentMode   = vk::PresentModeKHR::eFifo;

    vk::raii::SwapchainKHR mSwapchain;
    std::vector<vk::Image> mSwapchainImages;

    std::vector<std::shared_ptr<vk::raii::ImageView>> mImageViews;

    /** Index of the current swapchain image data */
    std::uint32_t mSwapchainImageSyncIndex = 0u;

    /** Array of sync elements for each image out of k frames in flight */
    std::vector<SwapchainImageSyncData> mSwapchainImageSyncData;

    std::optional<std::uint32_t> mCurrentSwapchainImageIndex;
};
} // namespace VOG::Graphics::Vulkan
