#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Semaphore.hpp>

#include <memory>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(SwapchainGeneration);

/**
 * Attachment for one acquired swapchain image. It shares ownership of the swapchain generation the
 * image belongs to, so the image, its view and the swapchain stay valid for as long as anything
 * records or holds it.
 *
 * Presenting the image releases its acquisition. An image that is dropped without being
 * presented is released in the destructor instead, so the swapchain can hand it out again.
 */
class AcquiredSwapchainImage final : public AttachmentInterface
{
public:
    /**
     * @note Render thread only. Releasing an unpresented image touches the swapchain, which is
     * externally synchronized.
     */
    ~AcquiredSwapchainImage() override;

    AcquiredSwapchainImage(const AcquiredSwapchainImage&) = delete;
    AcquiredSwapchainImage(AcquiredSwapchainImage&&)      = delete;

    /**
     * Signaled by the presentation engine once the image is free; the submit that renders into the
     * image waits it. It is the semaphore the caller passed to Swapchain::acquireNextImage.
     */
    const SemaphorePtr& imageAvailableSemaphore() const;

    const vk::Image&           getImage() const override;
    const vk::raii::ImageView& getImageView() const override;

private:
    friend class Swapchain;

    AcquiredSwapchainImage(SwapchainGenerationPtr generation,
                           std::uint32_t          imageIndex,
                           SemaphorePtr           imageAvailable);

    /** Called by Swapchain::present(): the presentation released the acquisition. */
    void markPresented();

    /** Declared first, so it is destroyed last: the image and its view live inside it. */
    SwapchainGenerationPtr mGeneration;

    std::uint32_t mImageIndex;
    SemaphorePtr  mImageAvailable;
    bool          mPresented = false;
};

VOG_DECLARE_PTR(AcquiredSwapchainImage);
} // namespace VOG::Graphics::Vulkan
