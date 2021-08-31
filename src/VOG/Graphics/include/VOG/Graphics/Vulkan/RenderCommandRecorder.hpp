#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/RenderStates.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <array>
#include <functional>
#include <optional>

namespace VOG::Graphics::Resources
{
VOG_DECLARE_PTR(Attachment);
VOG_DECLARE_PTR(RenderSurface);
} // namespace VOG::Graphics::Resources

namespace VOG::Graphics::Vulkan
{
class CommandBuffer;
class Device;
class RenderPass;
class RenderCommandRecorder
{
    static constexpr std::size_t kMaxAttachments = 1;

public:
    RenderCommandRecorder(const Device& device, Vulkan::CommandBuffer& commandBuffer);

    Vulkan::CommandBuffer* operator->();

    void beginRenderPass(std::shared_ptr<RenderPass> renderPass);

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
