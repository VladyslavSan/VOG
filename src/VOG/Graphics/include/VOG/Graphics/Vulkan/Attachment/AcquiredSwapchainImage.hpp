#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/Attachment/SwapchainGeneration.hpp>

#include <memory>

namespace VOG::Graphics::Vulkan
{
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

    const vk::Image&                            getImage() const override;
    const std::shared_ptr<vk::raii::ImageView>& getImageView() const override;

private:
    friend class Swapchain;

    AcquiredSwapchainImage(SwapchainGenerationPtr generation,
                           std::uint32_t          imageIndex,
                           vk::Format             format);

    /** Called by Swapchain::present(): the presentation released the acquisition. */
    void markPresented();

    /** Declared first, so it is destroyed last: everything below lives inside it. */
    SwapchainGenerationPtr mGeneration;

    std::uint32_t                        mImageIndex;
    bool                                 mPresented = false;
    vk::Image                            mImage;
    std::shared_ptr<vk::raii::ImageView> mImageView;
};

VOG_DECLARE_PTR(AcquiredSwapchainImage);
} // namespace VOG::Graphics::Vulkan
