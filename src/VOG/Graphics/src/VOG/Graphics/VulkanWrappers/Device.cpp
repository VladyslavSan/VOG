#include "VOG/Graphics/VulkanWrappers/Device.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/Converters.hpp>
#include <VOG/Graphics/Resources/RenderSurface.hpp>

#include <stdexcept>

namespace VOG::Graphics::VulkanWrappers
{
Device::~Device() {}

Device::Device(const Api::GraphicsProviderPtr& graphicsProvider)
    : mGraphicsProvider{graphicsProvider}
    , mDefaultRenderStates{RenderStates::DefaultRenderStates}
    , mCommandBuffer{}
{
}

void
Device::UseCommandBuffer(CommandBuffer commandBuffer)
{
    // Should have no value when use is called
    if (mCommandBuffer.has_value())
    {
        throw std::runtime_error{"Device::UseCommandBuffer internall command buffer is going to be "
                                 "discarded, check if it is desired"};
    }

    mCommandBuffer.emplace(std::move(commandBuffer));
}

void
Device::BeginRenderPass(const Resources::AttachmentPtr& color,
                        const Resources::AttachmentPtr& depthstencil)
{
    if (!mCommandBuffer)
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

    mAttachments[0] = color;
    mDepthStencilAttachment = depthstencil;
    for (std::size_t i = 1; i < MaxAttachments; ++i)
        mAttachments[i] = nullptr;

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

        mRenderPass = std::make_shared<vk::raii::RenderPass>(mGraphicsProvider->GetDevice(), ci);
        mCommandBuffer->AddBoundResource(mRenderPass);
    }

    {
        std::array<vk::ImageView, 2> attachmentsViews = {
            **color->GetImageView(),
            depthstencil ? **depthstencil->GetImageView() : vk::ImageView{}};
        auto extent = color->GetExtent();
        vk::FramebufferCreateInfo ci{};
        ci.setRenderPass(**mRenderPass)
            .setHeight(extent.height)
            .setWidth(extent.width)
            .setAttachments(attachmentsViews)
            .setAttachmentCount(depthstencil ? 2 : 1)
            .setLayers(1);

        mFramebuffer = std::make_shared<vk::raii::Framebuffer>(mGraphicsProvider->GetDevice(), ci);
        mCommandBuffer->AddBoundResource(mFramebuffer);
    }

    vk::CommandBufferBeginInfo beginInfo{};
    mCommandBuffer->begin(beginInfo);

    std::array<vk::ClearValue, 1> clearValues;
    clearValues[0].color = vk::ClearColorValue(std::array<float, 4>({{1.0f, 0.0f, 0.0f, 1.0f}}));
    // clearValues[1].depthStencil = vk::ClearDepthStencilValue(1.0f, 0);
    vk::RenderPassBeginInfo renderPassBeginInfo(
        **mRenderPass, **mFramebuffer,
        vk::Rect2D(vk::Offset2D(0, 0), ConvertTo<vk::Extent2D>(mAttachments[0]->GetExtent())),
        clearValues);

    mCommandBuffer->beginRenderPass(renderPassBeginInfo);
    mRenderPass.reset();
    mFramebuffer.reset();
}

void
Device::EndRenderPass()
{
    if (!mCommandBuffer)
        return;

    mCommandBuffer->endRenderPass();
}

CommandBuffer
Device::EndCommandBuffer()
{
    if (!mCommandBuffer)
    {
        throw std::runtime_error{
            "Device::EndCommandBuffer while not having a command buffer bound"};
    }

    mCommandBuffer->end();
    auto result = std::move(*mCommandBuffer);
    mCommandBuffer.reset();

    return std::move(result);
}

void
Device::OnFrameReset()
{
    ResetRenderStates();
    ResetAttachments();
}
} // namespace VOG::Graphics::VulkanWrappers
