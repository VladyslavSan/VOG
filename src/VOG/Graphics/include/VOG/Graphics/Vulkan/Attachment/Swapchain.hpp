#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(SwapchainGeneration);

class Swapchain final
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
        /** Frames in flight, asked for as the minimum image count. The surface limits can move the
         *  real count; read getImageCount(). */
        std::uint8_t framesInFlight = 1u;

        /** Priority list of presentation modes from most preferred to least. */
        std::vector<vk::PresentModeKHR> preferredPresentationModes = {
            vk::PresentModeKHR::eMailbox,
            vk::PresentModeKHR::eFifo,
            vk::PresentModeKHR::eFifoRelaxed};

        Common::SurfaceHandle surface;

        /**
         * Size in pixels, used only where the surface leaves the image size to the application
         * (it reports currentExtent as 0xFFFFFFFF, as Wayland and a headless surface do), and
         * required there: construction throws if it is 0x0. Ignored elsewhere.
         */
        vk::Extent2D extent = {.width = 0u, .height = 0u};
    };

    struct ImageAcquireResult
    {
        AcquireStatus status;
        /** Valid when eReady. Its imageAvailableSemaphore() is what the submit waits. */
        AcquiredSwapchainImagePtr acquired;
    };

    /**
     * What one vkQueuePresentKHR still uses after it returned. Hold it until the presentation is
     * over. Dropping it waits for the present fence, and only then lets go of the swapchain
     * generation and the semaphore the presentation waited.
     *
     * Move-only: a copy would be a second owner that waits the fence again.
     *
     * @note Render thread only. Dropping it returns the fence to its pool.
     */
    class Presentation
    {
        friend class Swapchain;

        Presentation(SwapchainGenerationPtr    generation,
                     SemaphorePtr              waitSemaphore,
                     FencePool::FenceHandlePtr fence);

    public:
        Presentation(Presentation&&) noexcept = default;

        /** Deleted: assigning would release the old generation before the old fence waited. */
        Presentation& operator=(Presentation&&) = delete;

        Presentation(const Presentation&)            = delete;
        Presentation& operator=(const Presentation&) = delete;

        /** Waits for the present fence (a moved-from Presentation has none), then lets go. */
        ~Presentation() noexcept;

    private:
        SwapchainGenerationPtr mGeneration;
        SemaphorePtr           mWaitSemaphore;
        /** Signals once the presentation engine is done with the semaphore and the swapchain. */
        FencePool::FenceHandlePtr mFence;
    };

    struct PresentResult
    {
        PresentStatus status;
        Presentation  presentation;
    };

    ~Swapchain();

private:
    friend class Device;

    Swapchain(DevicePtr device, const SwapchainParameters& parameters);

    /**
     * Creates a swapchain and its image views for the given extent.
     *
     * @param oldSwapchain  Previous swapchain handle for driver resource reuse (may be null).
     */
    SwapchainGenerationPtr makeGeneration(vk::SwapchainKHR oldSwapchain, vk::Extent2D extent);

public:
    /**
     * @param timeout         How long to wait for a free image; negative values count as zero. A
     *                        timeout gives eSkip, so a hidden window does not block the caller for
     *                        good.
     * @param imageAvailable  Signaled by the acquire when the image is free; the submit that
     *                        renders it waits it. It must be unsignaled and have no operation
     *                        pending, which is why the caller has to know that the wait of its
     *                        last use is done: a fence of the submit that waited it signaled
     *                        (VUID-vkAcquireNextImageKHR-semaphore-01779). It stays unaffected
     *                        unless the status is eReady. The result keeps it.
     */
    ImageAcquireResult acquireNextImage(std::chrono::nanoseconds timeout,
                                        SemaphorePtr             imageAvailable);

    /**
     * Presents @p acquired, which releases its acquisition. The presentation waits
     * @p waitSemaphore. The caller keeps the returned Presentation until the frame slot that
     * rendered the image starts again; see Presentation.
     *
     * @note Render thread only. Presenting uses the present queue, which is externally
     * synchronized.
     */
    PresentResult present(const AcquiredSwapchainImagePtr& acquired,
                          SemaphorePtr                     waitSemaphore,
                          FencePool&                       fencePool);

    /**
     * Recreates the swapchain to match the current surface size. The old swapchain is not
     * waited for: it dies once the last presentation and the last command buffer that use it
     * are done (see SwapchainGeneration).
     *
     * @param requested  Size in pixels, used only where the surface leaves the image size to the
     *                application, like SwapchainParameters::extent.
     *
     * @return true if the swapchain was rebuilt; false if skipped because the size is zero, from
     *         the surface or from @p requested (e.g. the window is minimized). It never throws for
     *         that, because the caller backs off on false.
     */
    bool recreate(vk::Extent2D requested);

    /** Number of images in the current swapchain. */
    std::size_t getImageCount() const;

    /** Size of the images the next acquire hands out: the extent of the current swapchain. */
    vk::Extent2D getExtent() const;

    /** Format of the swapchain images; pipelines that render into them must be built against it. */
    vk::Format getFormat() const;

private:
    DevicePtr                             mDevice;
    std::shared_ptr<vk::raii::SurfaceKHR> mSurface;

    const std::uint32_t mPresentQueueFamilyIndex;
    const bool          mPresentQueueIsSameToGraphicsQueue;
    vk::raii::Queue     mPresentQueue;

    vk::SurfaceFormatKHR mSurfaceFormat;

    /** Selected once at construction and reused on every rebuild. */
    std::uint32_t      mMinImageCount = 1u;
    vk::PresentModeKHR mPresentMode   = vk::PresentModeKHR::eFifo;

    /** Replaced by recreate(); acquired images and presentations keep the old one alive. */
    SwapchainGenerationPtr mGeneration;
};
} // namespace VOG::Graphics::Vulkan
