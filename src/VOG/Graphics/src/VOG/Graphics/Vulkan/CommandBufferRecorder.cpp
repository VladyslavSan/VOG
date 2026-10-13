#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>

#include <array>
#include <exception>

namespace VOG::Graphics::Vulkan
{
namespace
{
vk::RenderingAttachmentInfo
toAttachmentInfo(const CommandBufferRecorder::ColorAttachment& color)
{
    VOG_ASSERT_MSG(color.attachment, "Color attachment must not be null.");

    return {
        .imageView   = **color.attachment->getImageView(),
        .imageLayout = color.layout,
        .loadOp      = color.loadOp,
        .storeOp     = color.storeOp,
        .clearValue  = vk::ClearValue{color.clearValue},
    };
}

vk::RenderingAttachmentInfo
toAttachmentInfo(const CommandBufferRecorder::DepthAttachment& depth)
{
    VOG_ASSERT_MSG(depth.attachment, "Depth attachment must not be null.");
    VOG_ASSERT_MSG(isDepthFormat(depth.attachment->getFormat()) &&
                       !isStencilFormat(depth.attachment->getFormat()),
                   "Only depth-only formats are supported: no stencil attachment is set.");

    return {
        .imageView   = **depth.attachment->getImageView(),
        .imageLayout = depth.layout,
        .loadOp      = depth.loadOp,
        .storeOp     = depth.storeOp,
        .clearValue  = vk::ClearValue{depth.clearValue},
    };
}
} // namespace

CommandBufferRecorder::CommandBufferRecorder(Vulkan::CommandBuffer& commandBuffer)
    : mCommandBuffer{commandBuffer}
    , mUncaughtAtStart{std::uncaught_exceptions()}
{
    mCommandBuffer.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
}

CommandBufferRecorder::~CommandBufferRecorder()
{
    VOG_ASSERT_MSG(mFinished || std::uncaught_exceptions() > mUncaughtAtStart,
                   "CommandBufferRecorder destroyed without finish().");
}

void
CommandBufferRecorder::finish()
{
    VOG_ASSERT_MSG(!mFinished, "CommandBufferRecorder::finish() called twice.");
    VOG_ASSERT_MSG(!mRenderingActive, "finish() called while a RenderingPass is alive.");

    mCommandBuffer.end();
    mFinished = true;
}

CommandBufferRecorder::RenderingPass
CommandBufferRecorder::beginRendering(const ColorAttachments&               colorAttachments,
                                      const std::optional<DepthAttachment>& depthAttachment)
{
    return RenderingPass{*this, colorAttachments, depthAttachment};
}

CommandBufferRecorder::RenderingPass::RenderingPass(
    CommandBufferRecorder&                recorder,
    const ColorAttachments&               colorAttachments,
    const std::optional<DepthAttachment>& depthAttachment)
    : mRecorder{recorder}
{
    VOG_ASSERT_MSG(!mRecorder.mRenderingActive, "RenderingPass scopes must not nest.");
    VOG_ASSERT_MSG(!colorAttachments.empty() || depthAttachment.has_value(),
                   "beginRendering needs at least one attachment.");

    CommandBuffer& commandBuffer = mRecorder.mCommandBuffer;

    StaticVector<vk::RenderingAttachmentInfo, Limits::gMaxNumColorAttachments> colorInfos;
    vk::Extent2D                                                               extent{};

    for (const auto& color : colorAttachments)
    {
        colorInfos.push_back(toAttachmentInfo(color));
        VOG_ASSERT_MSG(colorInfos.size() == 1 || extent == color.attachment->getExtent2D(),
                       "All attachments of a rendering scope must share their extent.");

        extent = color.attachment->getExtent2D();
        commandBuffer.addBoundResource(color.attachment);
    }

    vk::RenderingAttachmentInfo depthInfo{};
    if (depthAttachment.has_value())
    {
        depthInfo = toAttachmentInfo(*depthAttachment);
        VOG_ASSERT_MSG(colorInfos.empty() || extent == depthAttachment->attachment->getExtent2D(),
                       "All attachments of a rendering scope must share their extent.");

        extent = depthAttachment->attachment->getExtent2D();
        commandBuffer.addBoundResource(depthAttachment->attachment);
    }

    commandBuffer.beginRendering({
        .renderArea           = {.offset = {.x = 0, .y = 0}, .extent = extent},
        .layerCount           = 1u,
        .colorAttachmentCount = static_cast<std::uint32_t>(colorInfos.size()),
        .pColorAttachments    = colorInfos.data(),
        .pDepthAttachment     = depthAttachment.has_value() ? &depthInfo : nullptr,
    });
    mRecorder.mRenderingActive = true;
}

CommandBufferRecorder::RenderingPass::~RenderingPass()
{
    mRecorder.mCommandBuffer.endRendering();
    mRecorder.mRenderingActive = false;
}

void
CommandBufferRecorder::RenderingPass::bindPipeline(GraphicsPipelinePtr pipeline)
{
    mRecorder.mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);
    mRecorder.mCommandBuffer.addBoundResource(std::move(pipeline));
}

void
CommandBufferRecorder::RenderingPass::bindVertexBuffers(
    std::uint32_t firstBinding,
    StaticVectorStrict<CommandBuffer::BufferBinding, CommandBuffer::kMaxNumVertexBufferBind>
        bindings)
{
    CommandBuffer& commandBuffer = mRecorder.mCommandBuffer;

    std::array<vk::Buffer, CommandBuffer::kMaxNumVertexBufferBind>     bufferHandles{};
    std::array<vk::DeviceSize, CommandBuffer::kMaxNumVertexBufferBind> offsets{};
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        bufferHandles[i] = **bindings[i].buffer;
        offsets[i]       = bindings[i].offset;
        commandBuffer.mBoundVertexBuffers.insert(std::move(bindings[i].buffer));
    }

    commandBuffer.bindVertexBuffers(
        firstBinding,
        {static_cast<std::uint32_t>(bindings.size()), bufferHandles.data()},
        {static_cast<std::uint32_t>(bindings.size()), offsets.data()});
}

void
CommandBufferRecorder::RenderingPass::setViewport(std::uint32_t                      firstViewport,
                                                  vk::ArrayProxy<const vk::Viewport> viewports)
{
    mRecorder.mCommandBuffer.setViewport(firstViewport, viewports);
}

void
CommandBufferRecorder::RenderingPass::setScissor(std::uint32_t                    firstScissor,
                                                 vk::ArrayProxy<const vk::Rect2D> scissors)
{
    mRecorder.mCommandBuffer.setScissor(firstScissor, scissors);
}

void
CommandBufferRecorder::RenderingPass::draw(std::uint32_t vertexCount,
                                           std::uint32_t instanceCount,
                                           std::uint32_t firstVertex,
                                           std::uint32_t firstInstance)
{
    mRecorder.mCommandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void
CommandBufferRecorder::setImageBarrier(const std::shared_ptr<void>&   resource,
                                       const vk::ImageMemoryBarrier2& barrier)
{
    mCommandBuffer.addBoundResource(resource);
    unsafeSetBarriers({}, {}, {barrier});
}

void
CommandBufferRecorder::setBufferBarrier(const std::shared_ptr<Buffer>& buffer,
                                        vk::BufferMemoryBarrier2       barrier)
{
    barrier.buffer = **buffer;
    mCommandBuffer.addBoundResource(buffer);
    unsafeSetBarriers({}, {barrier}, {});
}

void
CommandBufferRecorder::unsafeSetBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                                         vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                                         vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers)
{
    mCommandBuffer.pipelineBarrier2({.memoryBarrierCount       = memoryBarriers.size(),
                                     .pMemoryBarriers          = memoryBarriers.begin(),
                                     .bufferMemoryBarrierCount = bufferBarriers.size(),
                                     .pBufferMemoryBarriers    = bufferBarriers.begin(),
                                     .imageMemoryBarrierCount  = imageBarriers.size(),
                                     .pImageMemoryBarriers     = imageBarriers.begin()});
}
} // namespace VOG::Graphics::Vulkan
