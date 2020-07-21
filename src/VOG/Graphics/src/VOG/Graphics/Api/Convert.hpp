#pragma once

#include <VOG/Graphics/Api/RenderStates.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

namespace VOG::Graphics::Api
{
template <class T, class U> T ToVulkan(U);

template <>
vk::PolygonMode
ToVulkan<vk::PolygonMode, RenderStates::PolygonMode>(RenderStates::PolygonMode polygonMode)
{
    switch (polygonMode)
    {
    case RenderStates::PolygonMode::Fill:
        return vk::PolygonMode::eFill;
    case RenderStates::PolygonMode::Point:
        return vk::PolygonMode::ePoint;
    case RenderStates::PolygonMode::Line:
        return vk::PolygonMode::eLine;
    }

    return vk::PolygonMode::eFill;
}

template <>
vk::CullModeFlags
ToVulkan(RenderStates::CullMode cullMode)
{
    return static_cast<vk::CullModeFlags>(static_cast<RenderStates::EnumStateType>(cullMode));
}

template <>
vk::FrontFace
ToVulkan(RenderStates::FrontFace frontFace)
{
    return frontFace == RenderStates::FrontFace::CW ? vk::FrontFace::eClockwise
                                                    : vk::FrontFace::eCounterClockwise;
}

template <>
vk::CompareOp
ToVulkan(RenderStates::CompareOp op)
{
    switch (op)
    {
    case VOG::Graphics::core::RenderStates::CompareOp::Always:
        return vk::CompareOp::eAlways;
    case VOG::Graphics::core::RenderStates::CompareOp::Less:
        return vk::CompareOp::eLess;
    case VOG::Graphics::core::RenderStates::CompareOp::LessOrEqual:
        return vk::CompareOp::eLessOrEqual;
    case VOG::Graphics::core::RenderStates::CompareOp::Greater:
        return vk::CompareOp::eGreater;
    case VOG::Graphics::core::RenderStates::CompareOp::GreaterOrEqual:
        return vk::CompareOp::eGreaterOrEqual;
    case VOG::Graphics::core::RenderStates::CompareOp::Equal:
        return vk::CompareOp::eEqual;
    case VOG::Graphics::core::RenderStates::CompareOp::NotEqual:
        return vk::CompareOp::eNotEqual;
    case VOG::Graphics::core::RenderStates::CompareOp::Never:
        return vk::CompareOp::eNever;
    }

    return vk::CompareOp::eAlways;
}

template <>
vk::ColorComponentFlags
ToVulkan<vk::ColorComponentFlags, RenderStates::ColorWriteMask>(
    RenderStates::ColorWriteMask colorWriteMask)
{
    return static_cast<vk::ColorComponentFlagBits>(
        static_cast<RenderStates::EnumStateType>(colorWriteMask));
}

template <>
vk::BlendOp
ToVulkan(RenderStates::BlendOp op)
{
    switch (op)
    {
    case RenderStates::BlendOp::Add:
        return vk::BlendOp::eAdd;

    case RenderStates::BlendOp::Max:
        return vk::BlendOp::eMax;

    case RenderStates::BlendOp::Min:
        return vk::BlendOp::eMin;

    case RenderStates::BlendOp::Sub:
        return vk::BlendOp::eSubtract;

    case RenderStates::BlendOp::RevSub:
        return vk::BlendOp::eReverseSubtract;
    }

    return vk::BlendOp::eAdd;
}

template <>
vk::BlendFactor
ToVulkan(RenderStates::BlendFactor factor)
{
    switch (factor)
    {
    case RenderStates::BlendFactor::Zero:
        return vk::BlendFactor::eZero;
    case RenderStates::BlendFactor::One:
        return vk::BlendFactor::eOne;
    case RenderStates::BlendFactor::SrcColor:
        return vk::BlendFactor::eSrcColor;
    case RenderStates::BlendFactor::InvSrcColor:
        return vk::BlendFactor::eOneMinusSrcColor;
    case RenderStates::BlendFactor::DstColor:
        return vk::BlendFactor::eDstColor;
    case RenderStates::BlendFactor::InvDstColor:
        return vk::BlendFactor::eOneMinusDstColor;
    case RenderStates::BlendFactor::SrcAlpha:
        return vk::BlendFactor::eSrcAlpha;
    case RenderStates::BlendFactor::InvSrcAlpha:
        return vk::BlendFactor::eOneMinusSrcAlpha;
    case RenderStates::BlendFactor::DstAlpha:
        return vk::BlendFactor::eDstAlpha;
    case RenderStates::BlendFactor::InvDstAlpha:
        return vk::BlendFactor::eOneMinusDstAlpha;
    }

    return vk::BlendFactor::eOne;
}

vk::PipelineDepthStencilStateCreateInfo
ToVulkan(std::optional<RenderStates::DepthTest> depthTest,
         std::optional<RenderStates::StencilTest> stencilTest)
{
    vk::PipelineDepthStencilStateCreateInfo createInfo{};

    createInfo.setDepthTestEnable(depthTest.has_value() && depthTest->enabled);
    createInfo.setDepthWriteEnable(depthTest.has_value() && depthTest->writeEnabled);
    createInfo.setDepthCompareOp(depthTest.has_value()
                                     ? ToVulkan<vk::CompareOp>(depthTest->compareOp)
                                     : vk::CompareOp::eAlways);
    createInfo.setDepthBoundsTestEnable(false);

    const bool stencilTestEnabled = stencilTest.has_value() && stencilTest->enabled;
    createInfo.setStencilTestEnable(false);

    return createInfo;
}

vk::PipelineColorBlendAttachmentState
ToVulkan(std::optional<RenderStates::ColorBlend> colorBlend)
{
    vk::PipelineColorBlendAttachmentState state{};

    state.setColorWriteMask(ToVulkan<vk::ColorComponentFlags>(
        colorBlend.has_value() ? colorBlend->colorWriteMask : RenderStates::ColorWriteMask::RGBA));
    state.setBlendEnable(colorBlend.has_value() && colorBlend->blendEnabled);
    if (!state.blendEnable)
        return state;
    state.setColorBlendOp(ToVulkan<vk::BlendOp>(colorBlend->colorBlendOp));
    state.setSrcColorBlendFactor(ToVulkan<vk::BlendFactor>(colorBlend->srcColorBlendFactor));
    state.setSrcAlphaBlendFactor(ToVulkan<vk::BlendFactor>(colorBlend->srcAlphaBlendFactor));
    state.setAlphaBlendOp(ToVulkan<vk::BlendOp>(colorBlend->alphaBlendOp));
    state.setDstColorBlendFactor(ToVulkan<vk::BlendFactor>(colorBlend->dstColorBlendFactor));
    state.setDstAlphaBlendFactor(ToVulkan<vk::BlendFactor>(colorBlend->dstAlphaBlendFactor));

    return state;
}
} // namespace VOG::Graphics::Api