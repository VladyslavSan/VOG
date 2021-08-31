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
RenderCommandRecorder::beginRenderPass(std::shared_ptr<RenderPass> renderPass)
{

    mCommandBuffer.addBoundResource(renderPass);
    mCommandBuffer.beginRenderPass(renderPass->getBeginInfo(), {});
}

void
RenderCommandRecorder::endRenderPass()
{
    mCommandBuffer.endRenderPass();
}
} // namespace VOG::Graphics::Vulkan
