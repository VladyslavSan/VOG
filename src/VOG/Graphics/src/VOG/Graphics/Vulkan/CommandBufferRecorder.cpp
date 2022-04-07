#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"

#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>

#include <stdexcept>

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
    vk::RenderPassBeginInfo beginInfo{
        .renderPass      = **renderPass,
        .framebuffer     = **framebuffer,
        .renderArea      = {.offset = {.x = 0u, .y = 0u}, .extent = framebuffer->size()},
        .clearValueCount = static_cast<std::uint32_t>(clearValues.size()),
        .pClearValues    = clearValues.data()};
    mCommandBuffer.addBoundResource(renderPass);
    mCommandBuffer.addBoundResource(framebuffer);
    mCommandBuffer.beginRenderPass(beginInfo, {});
}

void
CommandBufferRecorder::endRenderPass() noexcept
{
    mCommandBuffer.endRenderPass();
}

void
CommandBufferRecorder::bindPipeline(const vk::PipelineBindPoint                pipelineBindPoint,
                                    const std::shared_ptr<vk::raii::Pipeline>& pipeline) noexcept
{
    mCommandBuffer.addBoundResource(pipeline);
    mCommandBuffer.bindPipeline(pipelineBindPoint, **pipeline);
}

void
CommandBufferRecorder::setBarriers(
    std::initializer_list<vk::MemoryBarrier2>       memoryBarriers,
    std::initializer_list<vk::BufferMemoryBarrier2> bufferBarriers,
    std::initializer_list<vk::ImageMemoryBarrier2>  imageBarriers) noexcept
{
    mCommandBuffer.pipelineBarrier2(
        {.memoryBarrierCount       = static_cast<uint32_t>(memoryBarriers.size()),
         .pMemoryBarriers          = memoryBarriers.begin(),
         .bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size()),
         .pBufferMemoryBarriers    = bufferBarriers.begin(),
         .imageMemoryBarrierCount  = static_cast<uint32_t>(imageBarriers.size()),
         .pImageMemoryBarriers     = imageBarriers.begin()});
}
} // namespace VOG::Graphics::Vulkan
