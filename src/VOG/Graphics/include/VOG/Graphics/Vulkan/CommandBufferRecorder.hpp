#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/FrameBuffer.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <array>
#include <functional>
#include <memory>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(CommandBuffer);
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderPass);
VOG_DECLARE_PTR(GraphicsPipeline);
VOG_DECLARE_PTR(Buffer);

class CommandBufferRecorder
{
public:
    using ClearValues = StaticVector<vk::ClearValue, Limits::gMaxNumAttachments>;

    CommandBufferRecorder(const Device& device, Vulkan::CommandBuffer& commandBuffer);

    Vulkan::CommandBuffer* operator->();

    void beginRenderPass(const RenderPassPtr&  renderPass,
                         const FramebufferPtr& framebuffer,
                         ClearValues           clearValues) noexcept;

    void endRenderPass() noexcept;

    void bindPipeline(GraphicsPipelinePtr pipeline) noexcept;

    /**
     * Image barrier that retains @p resource until the command buffer's fence fires.
     * @p barrier.image must already reference the underlying VkImage.
     */
    void setImageBarrier(const std::shared_ptr<void>& resource,
                         const vk::ImageMemoryBarrier2& barrier) noexcept;

    /**
     * Buffer barrier that retains @p buffer until the command buffer's fence fires.
     * Fills barrier.buffer from @p buffer.
     */
    void setBufferBarrier(const std::shared_ptr<Buffer>& buffer,
                          vk::BufferMemoryBarrier2       barrier) noexcept;

    /** Raw barriers with no retention — prefer setImageBarrier / setBufferBarrier. */
    void unsafeSetBarriers(
        vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
        vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
        vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept;

    [[deprecated("Use unsafeSetBarriers or setImageBarrier/setBufferBarrier")]]
    void setBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                     vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                     vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept;

protected:
    const Device&  mDevice;
    CommandBuffer& mCommandBuffer;

    // Render target states
    vk::Viewport mViewport;
    vk::Rect2D   mScissors;

    std::shared_ptr<RenderPass> mCurrentRenderPass;
};

VOG_DECLARE_PTR(CommandBufferRecorder);

inline Vulkan::CommandBuffer*
CommandBufferRecorder::operator->()
{
    return std::addressof(mCommandBuffer);
}
} // namespace VOG::Graphics::Vulkan
