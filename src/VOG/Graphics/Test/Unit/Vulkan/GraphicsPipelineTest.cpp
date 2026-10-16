#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

#include "VulkanFixture.hpp"

namespace VOG::Tests
{
namespace
{
const std::string gVertexShaderString = R"(
#version 450

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = inPosition;
    fragColor = inColor;
}
)";

/** Reads the color as signed integers, so only an integer format can feed location 1. */
const std::string gIntegerColorVertexShaderString = R"(
#version 450

layout(location = 0) in vec4 inPosition;
layout(location = 1) in ivec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = inPosition;
    fragColor = vec4(inColor);
}
)";

/** An array input covers locations 0 and 1. */
const std::string gArrayVertexShaderString = R"(
#version 450

layout(location = 0) in vec4 inPosition[2];

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = inPosition[0];
    fragColor = inPosition[1];
}
)";

const std::string gFragmentShaderGoodString = R"(
#version 450

layout(location = 0) in vec4 inColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = inColor;
}
)";
} // namespace

TEST_F(VulkanFixture, GraphicsPipeline_simpleTest)
{
    using namespace VOG::Graphics::Vulkan;

    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    const GraphicsPipeline::Parameters createInfoTemplate{
        .shading = {},
        .vertexLayout =
            {.bindingDescription =
                 {
                     {.binding = 0u, .stride = 64u, .inputRate = vk::VertexInputRate::eVertex},
                 },
             .attributeDescription = {{.location = 0u,
                                       .binding  = 0u,
                                       .format   = vk::Format::eR32G32B32A32Sfloat,
                                       .offset   = 0u},
                                      {.location = 1u,
                                       .binding  = 0u,
                                       .format   = vk::Format::eR32G32B32A32Sfloat,
                                       .offset   = 16u}}},
        .rasterizer    = {.cullMode = CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {},
        .blending      = {.attachments = {{.blendEnable = 0u, .colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {vk::Format::eR32G32B32A32Sfloat}}};

    auto fragmentShader = createShader(Shader::ShadingStage::eFragment, gFragmentShaderGoodString);

    // The template with the given vertex shader and one attribute per format, at the location
    // of its index.
    const auto makeCreateInfo =
        [&](const std::string& vertexSource, const std::vector<vk::Format>& formats)
    {
        auto createInfo    = createInfoTemplate;
        createInfo.shading = ShaderProgram::create(
            {.vertex   = {.shader     = createShader(Shader::ShadingStage::eVertex, vertexSource),
                          .entryPoint = "main"},
             .fragment = {.shader = fragmentShader, .entryPoint = "main"}});

        createInfo.vertexLayout.attributeDescription.clear();
        for (std::uint32_t location = 0u; location < formats.size(); ++location)
        {
            createInfo.vertexLayout.attributeDescription.push_back(
                {.location = location, .binding = 0u, .format = formats[location], .offset = 0u});
        }

        return createInfo;
    };

    constexpr auto kFloat4 = vk::Format::eR32G32B32A32Sfloat;
    constexpr auto kFloat2 = vk::Format::eR32G32Sfloat;

    {
        SCOPED_TRACE("correct pipeline compiles");
        EXPECT_NO_THROW(auto pipeline = mDevice->createGraphicsPipeline(
                            makeCreateInfo(gVertexShaderString, {kFloat4, kFloat4})););
    }
    {
        SCOPED_TRACE("a location the shader reads has no attribute");
        EXPECT_THROW(
            mDevice->createGraphicsPipeline(makeCreateInfo(gVertexShaderString, {kFloat4})),
            std::invalid_argument);
    }
    {
        SCOPED_TRACE("a float format cannot feed an integer input");
        EXPECT_THROW(mDevice->createGraphicsPipeline(
                         makeCreateInfo(gIntegerColorVertexShaderString, {kFloat4, kFloat4})),
                     std::invalid_argument);
        EXPECT_NO_THROW(
            auto pipeline = mDevice->createGraphicsPipeline(makeCreateInfo(
                gIntegerColorVertexShaderString, {kFloat4, vk::Format::eR32G32B32A32Sint})););
    }
    {
        SCOPED_TRACE("the component count is not compared");
        EXPECT_NO_THROW(auto pipeline = mDevice->createGraphicsPipeline(
                            makeCreateInfo(gVertexShaderString, {kFloat4, kFloat2})););
    }
    {
        SCOPED_TRACE("an attribute the shader does not read is allowed");
        EXPECT_NO_THROW(auto pipeline = mDevice->createGraphicsPipeline(
                            makeCreateInfo(gVertexShaderString, {kFloat4, kFloat4, kFloat4})););
    }
    {
        SCOPED_TRACE("every location of an array input needs an attribute");
        EXPECT_THROW(
            mDevice->createGraphicsPipeline(makeCreateInfo(gArrayVertexShaderString, {kFloat4})),
            std::invalid_argument);
        EXPECT_NO_THROW(auto pipeline = mDevice->createGraphicsPipeline(
                            makeCreateInfo(gArrayVertexShaderString, {kFloat4, kFloat4})););
    }
}
} // namespace VOG::Tests
