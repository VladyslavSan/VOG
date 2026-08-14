#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/FrameBuffer.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <memory>
#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderPass);
VOG_DECLARE_PTR(GraphicsPipeline);
VOG_DECLARE_PTR(Buffer);

/**
 * Only supported recording surface. All resource-referencing commands retain via
 * CommandBuffer::addBoundResource / mBoundVertexBuffers.
 */
class CommandBufferRecorder
{
public:
    using ClearValues = StaticVector<vk::ClearValue, Limits::gMaxNumAttachments>;

    CommandBufferRecorder(const Device& device, Vulkan::CommandBuffer& commandBuffer);

    void beginRenderPass(const RenderPassPtr&  renderPass,
                         const FramebufferPtr& framebuffer,
                         ClearValues           clearValues) noexcept;

    void endRenderPass() noexcept;

    void bindPipeline(GraphicsPipelinePtr pipeline) noexcept;

    void bindVertexBuffers(
        std::uint32_t firstBinding,
        StaticVectorStrict<CommandBuffer::BufferBinding, CommandBuffer::kMaxNumVertexBufferBind>
            bindings) noexcept;

    void setViewport(std::uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> viewports);

    void setScissor(std::uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> scissors);

    template <typename T>
    void pushConstants(vk::PipelineLayout      layout,
                       vk::ShaderStageFlags    stageFlags,
                       std::uint32_t           offset,
                       vk::ArrayProxy<const T> values)
    {
        mCommandBuffer.pushConstants(layout, stageFlags, offset, values);
    }

    void draw(std::uint32_t vertexCount,
              std::uint32_t instanceCount,
              std::uint32_t firstVertex,
              std::uint32_t firstInstance);

    /**
     * Image barrier that retains @p resource until the command buffer's fence fires.
     * @p barrier.image must already reference the underlying VkImage.
     */
    void setImageBarrier(const std::shared_ptr<void>&   resource,
                         const vk::ImageMemoryBarrier2& barrier) noexcept;

    /**
     * Buffer barrier that retains @p buffer until the command buffer's fence fires.
     * Fills barrier.buffer from @p buffer.
     */
    void setBufferBarrier(const std::shared_ptr<Buffer>& buffer,
                          vk::BufferMemoryBarrier2       barrier) noexcept;

    /** Raw barriers with no retention — prefer setImageBarrier / setBufferBarrier. */
    void unsafeSetBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                           vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                           vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept;

    [[deprecated("Use unsafeSetBarriers or setImageBarrier/setBufferBarrier")]]
    void setBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                     vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                     vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept;

protected:
    const Device&  mDevice;
    CommandBuffer& mCommandBuffer;
};

VOG_DECLARE_PTR(CommandBufferRecorder);
} // namespace VOG::Graphics::Vulkan
