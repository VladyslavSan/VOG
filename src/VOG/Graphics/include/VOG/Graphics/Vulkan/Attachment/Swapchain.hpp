#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
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

    /** All per-image resources share the same index: the value returned by vkAcquireNextImageKHR.
     */
    struct SwapchainImage
    {
        vk::Image                            image;
        std::shared_ptr<vk::raii::ImageView> imageView;
        /** PE signals this after finishing display of the image (PE→GPU "present done" signal).
         *  Populated via the swap trick in acquireNextImage(). Submit waits this. */
        vk::raii::Semaphore imageAvailableSemaphore;
        /** Signaled by the graphics submit; waited by present(). Safe to reuse once re-acquired. */
        vk::raii::Semaphore renderFinishedSemaphore;
    };

    struct ImageAcquireResult
    {
        AcquireStatus status;
        // valid when eReady; submit waits this
        const vk::raii::Semaphore* imageAvailableSemaphore;
        // valid when eReady; submit signals this
        const vk::raii::Semaphore* renderFinishedSemaphore;
    };

    ~Swapchain() override;

private:
    friend class Device;

    Swapchain(DevicePtr device, const SwapchainParameters& parameters);

    /**
     * (Re)creates the swapchain, image views and per-image sync data for the given extent.
     *
     * @param oldSwapchain  Previous swapchain handle for driver resource reuse (may be null).
     */
    void build(vk::SwapchainKHR oldSwapchain);

public:
    ImageAcquireResult acquireNextImage();

    PresentStatus present();

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

    vk::raii::SwapchainKHR      mSwapchain;
    std::vector<SwapchainImage> mImages;

    /** Rotates through mImages slots via the swap trick in acquireNextImage(). */
    vk::raii::Semaphore          mSpareAcquireSemaphore;
    std::optional<std::uint32_t> mCurrentSwapchainImageIndex;
};
} // namespace VOG::Graphics::Vulkan
