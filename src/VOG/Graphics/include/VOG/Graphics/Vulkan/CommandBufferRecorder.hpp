#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AttachmentInterface.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <memory>
#include <optional>
#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(GraphicsPipeline);
VOG_DECLARE_PTR(Buffer);

/**
 * Only supported recording surface, and the only way to retain a resource in a command buffer:
 * every command that references one retains it until the GPU is done.
 *
 * The recorder owns the command buffer it leased from CommandBufferPool::record(). It brackets the
 * recording state: the constructor begins the command buffer and finish() ends it and hands out the
 * ExecutableCommandBuffer. Dropping the recorder instead discards the recording. beginRendering()
 * hands out a RenderingPass that brackets a dynamic-rendering scope, and the draw commands exist
 * only on that pass.
 */
class CommandBufferRecorder
{
public:
    /** One dynamic-rendering color target: what to load, what to keep, what to clear to. */
    struct ColorAttachment
    {
        AttachmentInterfacePtr attachment;
        vk::AttachmentLoadOp   loadOp     = vk::AttachmentLoadOp::eClear;
        vk::AttachmentStoreOp  storeOp    = vk::AttachmentStoreOp::eStore;
        vk::ClearColorValue    clearValue = {};
        vk::ImageLayout        layout     = vk::ImageLayout::eColorAttachmentOptimal;
    };

    /** Depth-only target; formats with a stencil aspect are not supported. */
    struct DepthAttachment
    {
        AttachmentInterfacePtr     attachment;
        vk::AttachmentLoadOp       loadOp     = vk::AttachmentLoadOp::eClear;
        vk::AttachmentStoreOp      storeOp    = vk::AttachmentStoreOp::eDontCare;
        vk::ClearDepthStencilValue clearValue = {.depth = 1.0f, .stencil = 0u};
        vk::ImageLayout            layout     = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    };

    using ColorAttachments = StaticVector<ColorAttachment, Limits::gMaxNumColorAttachments>;

    /**
     * A dynamic-rendering scope: the constructor records vkCmdBeginRendering and the destructor
     * records vkCmdEndRendering. Must not outlive the recorder that created it.
     */
    class RenderingPass
    {
        friend class CommandBufferRecorder;

        RenderingPass(CommandBufferRecorder&                recorder,
                      const ColorAttachments&               colorAttachments,
                      const std::optional<DepthAttachment>& depthAttachment);

    public:
        ~RenderingPass();

        RenderingPass(const RenderingPass&)            = delete;
        RenderingPass(RenderingPass&&)                 = delete;
        RenderingPass& operator=(const RenderingPass&) = delete;
        RenderingPass& operator=(RenderingPass&&)      = delete;

        void bindPipeline(GraphicsPipelinePtr pipeline);

        void bindVertexBuffers(std::uint32_t firstBinding,
                               StaticVectorStrict<CommandBuffer::BufferBinding,
                                                  CommandBuffer::kMaxNumVertexBufferBind> bindings);

        void
        bindIndexBuffer(std::shared_ptr<Buffer> buffer, vk::DeviceSize offset, vk::IndexType type);

        void setViewport(std::uint32_t firstViewport, vk::ArrayProxy<const vk::Viewport> viewports);

        void setScissor(std::uint32_t firstScissor, vk::ArrayProxy<const vk::Rect2D> scissors);

        template <typename T>
        void
        pushConstants(vk::PipelineLayout      layout,
                      vk::ShaderStageFlags    stageFlags,
                      std::uint32_t           offset,
                      vk::ArrayProxy<const T> values)
        {
            mRecorder.mCommandBuffer->pushConstants(layout, stageFlags, offset, values);
        }

        void draw(std::uint32_t vertexCount,
                  std::uint32_t instanceCount,
                  std::uint32_t firstVertex,
                  std::uint32_t firstInstance);

        void drawIndexed(std::uint32_t indexCount,
                         std::uint32_t instanceCount,
                         std::uint32_t firstIndex,
                         std::int32_t  vertexOffset,
                         std::uint32_t firstInstance);

    private:
        CommandBufferRecorder& mRecorder;
    };

    /**
     * Discards the recording unless finish() ran. No RenderingPass may be alive.
     *
     * @note Render thread only. The pool takes no lock.
     */
    ~CommandBufferRecorder() noexcept;

    CommandBufferRecorder(const CommandBufferRecorder&)            = delete;
    CommandBufferRecorder(CommandBufferRecorder&&)                 = delete;
    CommandBufferRecorder& operator=(const CommandBufferRecorder&) = delete;
    CommandBufferRecorder& operator=(CommandBufferRecorder&&)      = delete;

    /** Ends the command buffer. No rendering pass may be alive. The recorder is empty afterwards.
     */
    [[nodiscard]] ExecutableCommandBuffer finish();

    /**
     * Starts a dynamic-rendering scope covering the full extent of the attachments.
     * All attachments must share their extent and are retained for the command buffer's lifetime.
     *
     * @param colorAttachments  Color targets with their load/store ops and clear values.
     * @param depthAttachment   Optional depth-only target.
     *
     * @return The scope; destroy it to end rendering.
     */
    [[nodiscard]] RenderingPass
    beginRendering(const ColorAttachments&               colorAttachments,
                   const std::optional<DepthAttachment>& depthAttachment = {});

    /**
     * Image barrier that retains @p attachment until the command buffer's fence fires. Fills
     * barrier.image from @p attachment, so @p barrier.image must be null.
     */
    void setImageBarrier(const AttachmentInterfacePtr& attachment, vk::ImageMemoryBarrier2 barrier);

    /**
     * Buffer barrier that retains @p buffer until the command buffer's fence fires.
     * Fills barrier.buffer from @p buffer.
     */
    void setBufferBarrier(const std::shared_ptr<Buffer>& buffer, vk::BufferMemoryBarrier2 barrier);

private:
    friend class CommandBufferPool;

    /** Raw barriers with no retention; setImageBarrier and setBufferBarrier retain, then call it.
     */
    void unsafeSetBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                           vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                           vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers);

    /** Begins @p commandBuffer, which the pool leased for this recorder. */
    CommandBufferRecorder(CommandBufferPoolPtr pool, std::unique_ptr<CommandBuffer> commandBuffer);

    CommandBufferPoolPtr           mPool;
    std::unique_ptr<CommandBuffer> mCommandBuffer;
    bool                           mRenderingActive = false;
};
} // namespace VOG::Graphics::Vulkan
