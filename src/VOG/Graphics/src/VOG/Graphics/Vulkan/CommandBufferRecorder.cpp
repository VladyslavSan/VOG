#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>

#include <array>

namespace VOG::Graphics::Vulkan
{
CommandBufferRecorder::CommandBufferRecorder(const Device&          device,
                                             Vulkan::CommandBuffer& commandBuffer)
    : mDevice{device}
    , mCommandBuffer{commandBuffer}
{
}

void
CommandBufferRecorder::beginRendering(
    const ColorAttachments&               colorAttachments,
    const std::optional<DepthAttachment>& depthAttachment) noexcept
{
    VOG_ASSERT_MSG(!colorAttachments.empty() || depthAttachment.has_value(),
                   "beginRendering needs at least one attachment.");

    StaticVector<vk::RenderingAttachmentInfo, Limits::gMaxNumColorAttachments> colorInfos;
    vk::Extent2D                                                               extent{};

    for (const auto& color : colorAttachments)
    {
        VOG_ASSERT_MSG(color.attachment, "Color attachment must not be null.");
        VOG_ASSERT_MSG(colorInfos.empty() || extent == color.attachment->getExtent2D(),
                       "All attachments of a rendering scope must share their extent.");

        extent = color.attachment->getExtent2D();
        colorInfos.push_back({
            .imageView   = **color.attachment->getImageView(),
            .imageLayout = color.layout,
            .loadOp      = color.loadOp,
            .storeOp     = color.storeOp,
            .clearValue  = vk::ClearValue{color.clearValue},
        });

        mCommandBuffer.addBoundResource(color.attachment);
    }

    vk::RenderingAttachmentInfo depthInfo{};
    if (depthAttachment.has_value())
    {
        const auto& depth = *depthAttachment;
        VOG_ASSERT_MSG(depth.attachment, "Depth attachment must not be null.");
        VOG_ASSERT_MSG(colorInfos.empty() || extent == depth.attachment->getExtent2D(),
                       "All attachments of a rendering scope must share their extent.");

        extent    = depth.attachment->getExtent2D();
        depthInfo = {
            .imageView   = **depth.attachment->getImageView(),
            .imageLayout = depth.layout,
            .loadOp      = depth.loadOp,
            .storeOp     = depth.storeOp,
            .clearValue  = vk::ClearValue{depth.clearValue},
        };

        mCommandBuffer.addBoundResource(depth.attachment);
    }

    mCommandBuffer.beginRendering({
        .renderArea           = {.offset = {.x = 0, .y = 0}, .extent = extent},
        .layerCount           = 1u,
        .colorAttachmentCount = static_cast<std::uint32_t>(colorInfos.size()),
        .pColorAttachments    = colorInfos.data(),
        .pDepthAttachment     = depthAttachment.has_value() ? &depthInfo : nullptr,
    });
}

void
CommandBufferRecorder::endRendering() noexcept
{
    mCommandBuffer.endRendering();
}

void
CommandBufferRecorder::bindPipeline(GraphicsPipelinePtr pipeline) noexcept
{
    mCommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);
    mCommandBuffer.addBoundResource(std::move(pipeline));
}

void
CommandBufferRecorder::bindVertexBuffers(
    std::uint32_t firstBinding,
    StaticVectorStrict<CommandBuffer::BufferBinding, CommandBuffer::kMaxNumVertexBufferBind>
        bindings) noexcept
{
    std::array<vk::Buffer, CommandBuffer::kMaxNumVertexBufferBind>     bufferHandles{};
    std::array<vk::DeviceSize, CommandBuffer::kMaxNumVertexBufferBind> offsets{};
    for (std::size_t i = 0; i < bindings.size(); ++i)
    {
        bufferHandles[i] = **bindings[i].buffer;
        offsets[i]       = bindings[i].offset;
        mCommandBuffer.mBoundVertexBuffers.insert(std::move(bindings[i].buffer));
    }

    mCommandBuffer.bindVertexBuffers(
        firstBinding,
        {static_cast<std::uint32_t>(bindings.size()), bufferHandles.data()},
        {static_cast<std::uint32_t>(bindings.size()), offsets.data()});
}

void
CommandBufferRecorder::setViewport(std::uint32_t                     firstViewport,
                                   vk::ArrayProxy<const vk::Viewport> viewports)
{
    mCommandBuffer.setViewport(firstViewport, viewports);
}

void
CommandBufferRecorder::setScissor(std::uint32_t                   firstScissor,
                                  vk::ArrayProxy<const vk::Rect2D> scissors)
{
    mCommandBuffer.setScissor(firstScissor, scissors);
}

void
CommandBufferRecorder::draw(std::uint32_t vertexCount,
                            std::uint32_t instanceCount,
                            std::uint32_t firstVertex,
                            std::uint32_t firstInstance)
{
    mCommandBuffer.draw(vertexCount, instanceCount, firstVertex, firstInstance);
}

void
CommandBufferRecorder::setImageBarrier(const std::shared_ptr<void>&   resource,
                                       const vk::ImageMemoryBarrier2& barrier) noexcept
{
    mCommandBuffer.addBoundResource(resource);
    unsafeSetBarriers({}, {}, {barrier});
}

void
CommandBufferRecorder::setBufferBarrier(const std::shared_ptr<Buffer>& buffer,
                                        vk::BufferMemoryBarrier2       barrier) noexcept
{
    barrier.buffer = **buffer;
    mCommandBuffer.addBoundResource(buffer);
    unsafeSetBarriers({}, {barrier}, {});
}

void
CommandBufferRecorder::unsafeSetBarriers(
    vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
    vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
    vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept
{
    mCommandBuffer.pipelineBarrier2({.memoryBarrierCount       = memoryBarriers.size(),
                                     .pMemoryBarriers          = memoryBarriers.begin(),
                                     .bufferMemoryBarrierCount = bufferBarriers.size(),
                                     .pBufferMemoryBarriers    = bufferBarriers.begin(),
                                     .imageMemoryBarrierCount  = imageBarriers.size(),
                                     .pImageMemoryBarriers     = imageBarriers.begin()});
}

void
CommandBufferRecorder::setBarriers(vk::ArrayProxy<vk::MemoryBarrier2>       memoryBarriers,
                                   vk::ArrayProxy<vk::BufferMemoryBarrier2> bufferBarriers,
                                   vk::ArrayProxy<vk::ImageMemoryBarrier2>  imageBarriers) noexcept
{
    unsafeSetBarriers(memoryBarriers, bufferBarriers, imageBarriers);
}
} // namespace VOG::Graphics::Vulkan
