#include "VOG/Graphics/Vulkan/RenderCommandRecorder.hpp"

#include <VOG/Graphics/Vulkan/CommandBuffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
RenderCommandRecorder::RenderCommandRecorder(const Device&          device,
                                             Vulkan::CommandBuffer& commandBuffer)
    : mDevice{device}
    , mCommandBuffer{commandBuffer}
{
}

void
RenderCommandRecorder::beginRenderPass(RenderPassPtr  renderPass,
                                       FramebufferPtr framebuffer,
                                       ClearValues    clearValues)
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
RenderCommandRecorder::endRenderPass()
{
    mCommandBuffer.endRenderPass();
}
} // namespace VOG::Graphics::Vulkan
