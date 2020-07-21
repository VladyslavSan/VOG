#include "VOG/Graphics/Api/Device.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Converters.hpp>
#include <VOG/Graphics/Resources/RenderSurface.hpp>

#include <stdexcept>

namespace VOG::Graphics::Api
{
Device::~Device() {}

Device::Device(const GraphicsProviderPtr& graphicsProvider)
    : m_graphicsProvider{graphicsProvider}
    , m_defaultRenderStates{RenderStates::DefaultRenderStates}
{
}

void
Device::UseCommandBuffer(std::unique_ptr<vk::raii::CommandBuffer> commandBuffer)
{
    m_commandBuffer = std::move(commandBuffer);
}

void
Device::BeginRenderPass(const Resources::AttachmentPtr& color,
                        const Resources::AttachmentPtr& depthstencil)
{
    if (!m_commandBuffer)
    {
        throw std::runtime_error{"BeginRenderPass failed. Command buffer is not set."};
    }
    if (!color)
    {
        throw std::runtime_error{"BeginRenderPass failed. Color attachment is empty."};
    }

    if (depthstencil && color->GetExtent() != depthstencil->GetExtent())
    {
        throw std::runtime_error{
            "BeginRenderPass failed. Color and depth attachments have different extents."};
    }

    m_attachments[0] = color;
    m_depthStencilAttachment = depthstencil;
    for (std::size_t i = 1; i < MaxAttachments; ++i)
        m_attachments[i] = nullptr;

    std::array<vk::AttachmentDescription, 2> description;
    description[0].setFormat(ConvertTo<vk::Format>(color->GetFormat()));
    description[0].setSamples(ConvertTo<vk::SampleCountFlagBits>(color->GetNumSamples()));
    description[0].setLoadOp(vk::AttachmentLoadOp::eClear);
    description[0].setStoreOp(vk::AttachmentStoreOp::eStore);
    description[0].setInitialLayout(vk::ImageLayout::eUndefined);
    description[0].setFinalLayout(vk::ImageLayout::ePresentSrcKHR);

    if (depthstencil)
    {
        description[1].setFormat(ConvertTo<vk::Format>(depthstencil->GetFormat()));
        description[1].setSamples(
            ConvertTo<vk::SampleCountFlagBits>(depthstencil->GetNumSamples()));
        description[1].setLoadOp(vk::AttachmentLoadOp::eClear);
        description[1].setStoreOp(vk::AttachmentStoreOp::eStore);
        description[1].setStencilLoadOp(vk::AttachmentLoadOp::eClear);
        description[1].setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);
        description[1].setInitialLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
        description[1].setFinalLayout(vk::ImageLayout::ePresentSrcKHR);
    }

    std::array<vk::AttachmentReference, 2> attachmentReference;
    attachmentReference[0].setAttachment(0);
    attachmentReference[0].setLayout(vk::ImageLayout::eColorAttachmentOptimal);

    if (depthstencil)
    {
        attachmentReference[1].setAttachment(1);
        attachmentReference[1].setLayout(vk::ImageLayout::eDepthAttachmentOptimal);
    }

    vk::SubpassDescription subpass;
    subpass.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);
    subpass.setColorAttachments(attachmentReference[0]);
    if (depthstencil)
        subpass.setPDepthStencilAttachment(&attachmentReference[1]);

    {
        vk::RenderPassCreateInfo ci{};
        ci.setAttachments(description);
        ci.setAttachmentCount(depthstencil ? 2 : 1);
        ci.setSubpassCount(1);
        ci.setSubpasses(subpass);

        m_renderPass = std::make_unique<vk::raii::RenderPass>(*m_graphicsProvider->GetDevice(), ci);
    }

    {
        std::array<vk::ImageView, 2> attachmentsViews = {
            **color->GetImageView(),
            depthstencil ? **depthstencil->GetImageView() : vk::ImageView{}};
        auto extent = color->GetExtent();
        vk::FramebufferCreateInfo ci{};
        ci.setRenderPass(**m_renderPass)
            .setHeight(extent.height)
            .setWidth(extent.width)
            .setAttachments(attachmentsViews)
            .setAttachmentCount(depthstencil ? 2 : 1)
            .setLayers(1);

        m_framebuffer =
            std::make_unique<vk::raii::Framebuffer>(*m_graphicsProvider->GetDevice(), ci);
    }

    vk::CommandBufferBeginInfo beginInfo{};
    m_commandBuffer->begin({});

    std::array<vk::ClearValue, 1> clearValues;
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>({{1.0f, 0.0f, 0.0f, 1.0f}}));
    // clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderPassBeginInfo renderPassBeginInfo(
        **m_renderPass, **m_framebuffer,
        vk::Rect2D(vk::Offset2D(0, 0), ConvertTo<vk::Extent2D>(m_attachments[0]->GetExtent())),
        clearValues);

    m_commandBuffer->beginRenderPass(renderPassBeginInfo, vk::SubpassContents::eInline);
}

void
Device::EndRenderPass()
{
    if (!m_commandBuffer)
        return;

    m_commandBuffer->endRenderPass();
}

std::unique_ptr<vk::raii::CommandBuffer>
Device::EndCommandBuffer()
{
    m_commandBuffer->end();
    return std::move(m_commandBuffer);
}

void
Device::OnFrameReset()
{
    ResetRenderStates();
    ResetAttachments();
}
} // namespace VOG::Graphics::Api
