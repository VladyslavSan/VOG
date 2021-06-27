#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/VulkanWrappers/CommandBuffer.hpp>
#include <VOG/Graphics/VulkanWrappers/RenderStates.hpp>

#include <array>
#include <optional>
#include <stack>
#include <variant>

namespace VOG::Graphics
{
namespace Api
{
VOG_DECLARE_PTR(GraphicsProvider);
}
namespace Resources
{
VOG_DECLARE_PTR(Attachment);
VOG_DECLARE_PTR(RenderSurface);
} // namespace Resources

namespace VulkanWrappers
{
class CommandBuffer;

class Device
{
    static constexpr std::size_t MaxAttachments = 8;

    friend class SubmissionController;

public:
    using OptionalSemaphoreRef = std::reference_wrapper<vk::raii::Semaphore>;

    ~Device();

    Device(const Api::GraphicsProviderPtr& graphicsProvider);

    void SetDefaultRenderStates(const RenderStates::RenderState& renderStates);

    void SetRenderState(RenderStates::PolygonMode polygonMode);
    void SetRenderState(std::optional<RenderStates::DepthTest> depthTest);
    void SetRenderState(std::optional<RenderStates::StencilTest> stencilTest);
    void SetRenderState(std::optional<RenderStates::ColorBlend> colorBlend);

    void UseCommandBuffer(VulkanWrappers::CommandBuffer commandBuffer);

    /**
     * Start a simple render pass with color and optional depthstencil attachments
     *
     * @color Color attachment to render into.
     * @depthstencil Depth stencil attachment to use as depth+stencil buffer.
     *
     * @throw std::runtime_error in case of any errors due to missuse etc.
     */
    void BeginRenderPass(const Resources::AttachmentPtr& color,
                         const Resources::AttachmentPtr& depthstencil);

    /**
     * End currently active render pass
     */
    void EndRenderPass();

    /**
     * Getter for render states.
     *
     * @note Should not be used frequently, for debug purposes only.
     */
    const RenderStates::RenderState& GetRenderStates() const;

    std::optional<std::size_t> GetCurrentSubpassStage() const;

    CommandBuffer EndCommandBuffer();

    void ResetRenderStates();
    void ResetAttachments();
    void OnFrameReset();

protected:
    // Object references
    Api::GraphicsProviderPtr mGraphicsProvider;

    // States
    RenderStates::RenderState mDefaultRenderStates;
    RenderStates::RenderState mRenderStates;

    vk::Viewport mViewport;
    vk::Rect2D mScissors;

    // Resources
    std::array<Resources::AttachmentPtr, MaxAttachments> mAttachments;
    Resources::AttachmentPtr mDepthStencilAttachment;

    std::optional<VulkanWrappers::CommandBuffer> mCommandBuffer;
    std::unique_ptr<vk::raii::PipelineCache> mPipelineCache;
    std::shared_ptr<vk::raii::RenderPass> mRenderPass;
    std::shared_ptr<vk::raii::Framebuffer> mFramebuffer;
};
VOG_DECLARE_PTR(Device);

inline void
Device::SetDefaultRenderStates(const RenderStates::RenderState& renderStates)
{
    mDefaultRenderStates = renderStates;
}

inline void
Device::SetRenderState(RenderStates::PolygonMode polygonMode)
{
    mRenderStates.polygonMode = polygonMode;
}

inline void
Device::SetRenderState(std::optional<RenderStates::DepthTest> depthTest)
{
    mRenderStates.depthTest = depthTest;
}

inline void
Device::SetRenderState(std::optional<RenderStates::StencilTest> stencilTest)
{
    mRenderStates.stencilTest = stencilTest;
}

inline void
Device::SetRenderState(std::optional<RenderStates::ColorBlend> colorBlend)
{
    mRenderStates.colorBlend = colorBlend;
}

inline const RenderStates::RenderState&
Device::GetRenderStates() const
{
    return mRenderStates;
}

inline std::optional<std::size_t>
Device::GetCurrentSubpassStage() const
{
    return {};
}

inline void
Device::ResetRenderStates()
{
    mRenderStates = mDefaultRenderStates;
}

inline void
Device::ResetAttachments()
{
    for (std::size_t i = 0; i < MaxAttachments; ++i)
    {
        mAttachments[i].reset();
    }
}
} // namespace VulkanWrappers
} // namespace VOG::Graphics
