#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/FrameBuffer.hpp>

#include <array>
#include <functional>

namespace VOG::Graphics::Vulkan
{
class CommandBuffer;
class Device;
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderPass);
class CommandBufferRecorder
{
    static constexpr std::size_t kMaxAttachments = 1;

public:
    using ClearValues =
        boost::container::small_vector<vk::ClearValue, Framebuffer::kMaxNumAttachments>;
    CommandBufferRecorder(const Device& device, Vulkan::CommandBuffer& commandBuffer);

    Vulkan::CommandBuffer* operator->();

    void beginRenderPass(const RenderPassPtr&  renderPass,
                         const FramebufferPtr& framebuffer,
                         ClearValues           clearValues) noexcept;

    void endRenderPass() noexcept;

    void bindPipeline(vk::PipelineBindPoint                      pipelineBindPoint,
                      const std::shared_ptr<vk::raii::Pipeline>& pipeline) noexcept;

    void setBarriers(std::initializer_list<vk::MemoryBarrier2>       memoryBarriers,
                     std::initializer_list<vk::BufferMemoryBarrier2> bufferBarriers,
                     std::initializer_list<vk::ImageMemoryBarrier2>  imageBarriers) noexcept;

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
