#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Framebuffer.hpp>

#include <array>
#include <functional>

namespace VOG::Graphics::Vulkan
{
class CommandBuffer;
class Device;
VOG_DECLARE_PTR(Framebuffer);
VOG_DECLARE_PTR(RenderPass);
class RenderCommandRecorder
{
    static constexpr std::size_t kMaxAttachments = 1;

public:
    using ClearValues =
        boost::container::small_vector<vk::ClearValue, Framebuffer::kMaxNumAttachments>;
    RenderCommandRecorder(const Device& device, Vulkan::CommandBuffer& commandBuffer);

    Vulkan::CommandBuffer* operator->();

    void
    beginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer, ClearValues clearValues);

    void endRenderPass();

protected:
    const Device&  mDevice;
    CommandBuffer& mCommandBuffer;

    // Render target states
    vk::Viewport mViewport;
    vk::Rect2D   mScissors;

    std::shared_ptr<RenderPass> mCurrentRenderPass;
};
VOG_DECLARE_PTR(RenderCommandRecorder);

inline Vulkan::CommandBuffer*
RenderCommandRecorder::operator->()
{
    return std::addressof(mCommandBuffer);
}
} // namespace VOG::Graphics::Vulkan
