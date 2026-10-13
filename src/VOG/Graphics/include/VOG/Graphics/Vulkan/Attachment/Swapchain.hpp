#pragma once

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

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
        /** Frames in flight, might be lower than requested. */
        std::uint8_t framesInFlight = 1u;

        /** Priority list of presentation modes from most preferred to least. */
        std::vector<vk::PresentModeKHR> preferredPresentationModes = {
            vk::PresentModeKHR::eMailbox,
            vk::PresentModeKHR::eFifo,
            vk::PresentModeKHR::eFifoRelaxed};

        Common::SurfaceHandle surface;

        /**
         * Size to use when the surface lets the application choose it (the surface reports
         * currentExtent as 0xFFFFFFFF, as a headless surface does). Ignored otherwise.
         */
        vk::Extent2D fallbackExtent = {.width = 0u, .height = 0u};
    };

    struct ImageAcquireResult
    {
        AcquireStatus status;
        /** Valid when eReady. */
        AcquiredSwapchainImagePtr acquired;
        /** Valid when eReady; submit waits this. */
        SemaphorePtr imageAvailableSemaphore;
    };

    /**
     * What one vkQueuePresentKHR still uses after it returned. Hold it until the presentation is
     * over: dropping it waits for the present fence, and only then lets go of the swapchain
     * generation and the semaphore the presentation waited.
     *
     * Members are destroyed in reverse order, so the fence goes first.
     *
     * @note Render thread only. Dropping it returns the fence to its pool.
     */
    struct Presentation
    {
        SwapchainGenerationPtr generation;
        SemaphorePtr           waitSemaphore;
        /** Signals once the presentation engine is done with the semaphore and the swapchain. */
        std::shared_ptr<FencePool::FenceHandle> fence;
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
     * Creates a swapchain, its image views and per-image sync data for the given extent.
     *
     * @param oldSwapchain  Previous swapchain handle for driver resource reuse (may be null).
     */
    SwapchainGenerationPtr makeGeneration(vk::SwapchainKHR oldSwapchain, vk::Extent2D extent);

public:
    ImageAcquireResult acquireNextImage();

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
                          const FencePoolPtr&              fencePool);

    /**
     * Recreates the swapchain to match the current surface size. The old swapchain is not
     * waited for: it dies once the last presentation and the last command buffer that use it
     * are done (see SwapchainGeneration).
     *
     * @return true if the swapchain was rebuilt; false if skipped because the surface is currently
     *         zero-sized (e.g. the window is minimized).
     */
    bool recreate();

    /** Number of images currently owned by the swapchain (after the last build). */
    std::size_t getImageCount() const;

    /** Format of the swapchain images; pipelines that render into them must be built against it. */
    vk::Format getFormat() const;

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

    /** Replaced by recreate(); acquired images and presentations keep the old one alive. */
    SwapchainGenerationPtr mGeneration;
};
} // namespace VOG::Graphics::Vulkan
