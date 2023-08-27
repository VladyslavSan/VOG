#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/FrameBuffer.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <array>
#include <functional>

namespace VOG::Graphics::Vulkan
{
class CommandBuffer;
class Device;
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderPass);
VOG_DECLARE_PTR(GraphicsPipeline);

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
