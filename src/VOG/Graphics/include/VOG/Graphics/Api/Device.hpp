#pragma once

#include <VOG/Graphics/Api/RenderStates.hpp>
#include <VOG/Graphics/Typedefs.hpp>

#include <array>
#include <optional>
#include <stack>
#include <variant>

namespace vk::raii
{
class RenderPass;
class PipelineCache;
class Framebuffer;
class CommandPool;
class CommandBuffer;
class Semaphore;
class Fence;
} // namespace vk::raii

namespace VOG::Graphics
{
namespace Resources
{
VOG_DECLARE_PTR(Attachment);
VOG_DECLARE_PTR(RenderSurface);
} // namespace Resources
namespace Api
{
VOG_DECLARE_PTR(GraphicsProvider);

class Device
{
    static constexpr std::size_t MaxAttachments = 8;

    friend class SubmissionController;

public:
    using OptionalSemaphoreRef = std::reference_wrapper<vk::raii::Semaphore>;

    ~Device();

    Device(const GraphicsProviderPtr& graphicsProvider);

    void SetDefaultRenderStates(const RenderStates::RenderState& renderStates);

    void SetRenderState(RenderStates::PolygonMode polygonMode);
    void SetRenderState(std::optional<RenderStates::DepthTest> depthTest);
    void SetRenderState(std::optional<RenderStates::StencilTest> stencilTest);
    void SetRenderState(std::optional<RenderStates::ColorBlend> colorBlend);

    void UseCommandBuffer(std::unique_ptr<vk::raii::CommandBuffer> commandBuffer);

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

    std::unique_ptr<vk::raii::CommandBuffer> EndCommandBuffer();

    void ResetRenderStates();
    void ResetAttachments();
    void OnFrameReset();

protected:
    // Object references
    GraphicsProviderPtr m_graphicsProvider;

    // States
    RenderStates::RenderState m_defaultRenderStates;
    RenderStates::RenderState m_renderStates;
    std::pair<std::int32_t, std::int32_t> m_viewport;
    std::pair<std::int32_t, std::int32_t> m_scissors;

    // Resources
    std::array<Resources::AttachmentPtr, MaxAttachments> m_attachments;
    Resources::AttachmentPtr m_depthStencilAttachment;

    std::unique_ptr<vk::raii::CommandBuffer> m_commandBuffer;

    std::unique_ptr<vk::raii::PipelineCache> m_pipelineCache;
    std::unique_ptr<vk::raii::RenderPass> m_renderPass;
    std::unique_ptr<vk::raii::Framebuffer> m_framebuffer;
};
VOG_DECLARE_PTR(Device);

inline void
Device::SetDefaultRenderStates(const RenderStates::RenderState& renderStates)
{
    m_defaultRenderStates = renderStates;
}

inline void
Device::SetRenderState(RenderStates::PolygonMode polygonMode)
{
    m_renderStates.polygonMode = polygonMode;
}

inline void
Device::SetRenderState(std::optional<RenderStates::DepthTest> depthTest)
{
    m_renderStates.depthTest = depthTest;
}

inline void
Device::SetRenderState(std::optional<RenderStates::StencilTest> stencilTest)
{
    m_renderStates.stencilTest = stencilTest;
}

inline void
Device::SetRenderState(std::optional<RenderStates::ColorBlend> colorBlend)
{
    m_renderStates.colorBlend = colorBlend;
}

inline const RenderStates::RenderState&
Device::GetRenderStates() const
{
    return m_renderStates;
}

inline std::optional<std::size_t>
Device::GetCurrentSubpassStage() const
{
    return {};
}

inline void
Device::ResetRenderStates()
{
    m_renderStates = m_defaultRenderStates;
}

inline void
Device::ResetAttachments()
{
    for (std::size_t i = 0; i < MaxAttachments; ++i)
    {
        m_attachments[i].reset();
    }
}
} // namespace Api
} // namespace VOG::Graphics
