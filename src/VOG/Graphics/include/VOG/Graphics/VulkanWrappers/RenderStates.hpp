#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstdint>
#include <optional>

namespace VOG::Graphics::VulkanWrappers::RenderStates
{
using PolygonMode = vk::PolygonMode;

struct DepthBias
{
    float constant;
    float clamp;
    float slope;

    auto operator<=>(const DepthBias&) const = default;
};

struct DepthTest
{
    bool enabled = false;
    bool writeEnabled = false;
    vk::CompareOp compareOp = vk::CompareOp::eAlways;

    auto operator<=>(const DepthTest&) const = default;
};

struct StencilFaceOp
{
    vk::StencilOp stencilFailOp = vk::StencilOp::eKeep;
    vk::StencilOp stencilPassOp = vk::StencilOp::eKeep;
    vk::StencilOp depthFailOp = vk::StencilOp::eKeep;

    auto operator<=>(const StencilFaceOp&) const = default;
};

struct StencilTest
{
    bool enabled = false;
    StencilFaceOp front = {.stencilFailOp = vk::StencilOp::eKeep,
                           .stencilPassOp = vk::StencilOp::eKeep,
                           .depthFailOp = vk::StencilOp::eKeep};
    StencilFaceOp back = {.stencilFailOp = vk::StencilOp::eKeep,
                          .stencilPassOp = vk::StencilOp::eKeep,
                          .depthFailOp = vk::StencilOp::eKeep};

    auto operator<=>(const StencilTest&) const = default;
};

struct ColorBlend
{
    vk::ColorComponentFlags colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    bool blendEnabled = false;
    vk::BlendFactor srcColorBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstColorBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp colorBlendOp = vk::BlendOp::eAdd;
    vk::BlendFactor srcAlphaBlendFactor = vk::BlendFactor::eOne;
    vk::BlendFactor dstAlphaBlendFactor = vk::BlendFactor::eZero;
    vk::BlendOp alphaBlendOp = vk::BlendOp::eAdd;

    auto operator<=>(const ColorBlend&) const = default;
};

struct RenderState
{
    vk::PolygonMode polygonMode;
    vk::CullModeFlags cullMode;
    vk::FrontFace frontFace;
    std::optional<DepthBias> depthBias;
    std::optional<DepthTest> depthTest;
    std::optional<StencilTest> stencilTest;
    std::optional<ColorBlend> colorBlend;
};

constexpr RenderState DefaultRenderStates{
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eClockwise,
    .depthBias = {},
    .depthTest =
        DepthTest{.enabled = false, .writeEnabled = false, .compareOp = vk::CompareOp::eAlways},
    .stencilTest = StencilTest{},
    .colorBlend = ColorBlend{}};

vk::PipelineDepthStencilStateCreateInfo inline ToVulkan(
    std::optional<RenderStates::DepthTest> depthTest,
    std::optional<RenderStates::StencilTest> stencilTest)
{
    vk::PipelineDepthStencilStateCreateInfo createInfo{};

    createInfo.setDepthTestEnable(depthTest.has_value() && depthTest->enabled);
    createInfo.setDepthWriteEnable(depthTest.has_value() && depthTest->writeEnabled);
    createInfo.setDepthCompareOp(depthTest.has_value() ? depthTest->compareOp
                                                       : vk::CompareOp::eAlways);
    createInfo.setDepthBoundsTestEnable(false);

    const bool stencilTestEnabled = false; // stencilTest.has_value() && stencilTest->enabled;
    createInfo.setStencilTestEnable(stencilTestEnabled);

    return createInfo;
}

vk::PipelineColorBlendAttachmentState inline ToVulkan(
    std::optional<RenderStates::ColorBlend> colorBlend)
{
    vk::PipelineColorBlendAttachmentState state{};

    state.setColorWriteMask(colorBlend.has_value()
                                ? colorBlend->colorWriteMask
                                : vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                      vk::ColorComponentFlagBits::eB |
                                      vk::ColorComponentFlagBits::eA);
    state.setBlendEnable(colorBlend.has_value() && colorBlend->blendEnabled);
    if (!state.blendEnable)
        return state;
    state.setColorBlendOp(colorBlend->colorBlendOp);
    state.setSrcColorBlendFactor(colorBlend->srcColorBlendFactor);
    state.setSrcAlphaBlendFactor(colorBlend->srcAlphaBlendFactor);
    state.setAlphaBlendOp(colorBlend->alphaBlendOp);
    state.setDstColorBlendFactor(colorBlend->dstColorBlendFactor);
    state.setDstAlphaBlendFactor(colorBlend->dstAlphaBlendFactor);

    return state;
}
} // namespace VOG::Graphics::VulkanWrappers::RenderStates
