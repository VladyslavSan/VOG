#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>

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
CommandBufferRecorder::beginRenderPass(const RenderPassPtr&  renderPass,
                                       const FramebufferPtr& framebuffer,
                                       ClearValues           clearValues) noexcept
{
    VOG_ASSERT_MSG(renderPass->getRenderpassDescription() ==
                       framebuffer->getRenderpassDescription(),
                   "Renderpass and Framebuffer are incompatible.");

    mCommandBuffer.beginRenderPass(
        {.renderPass      = **renderPass,
         .framebuffer     = **framebuffer,
         .renderArea      = {.offset = {.x = 0u, .y = 0u}, .extent = framebuffer->extent()},
         .clearValueCount = static_cast<std::uint32_t>(clearValues.size()),
         .pClearValues    = clearValues.data()},
        {});

    mCommandBuffer.addBoundResource(framebuffer);
    mCommandBuffer.addBoundResource(renderPass);
}

void
CommandBufferRecorder::endRenderPass() noexcept
{
    mCommandBuffer.endRenderPass();
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
