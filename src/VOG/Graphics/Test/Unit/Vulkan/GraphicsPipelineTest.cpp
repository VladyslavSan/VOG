#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

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

    vk::raii::PipelineLayout pipelineLayout{*VulkanDevice, vk::PipelineLayoutCreateInfo{}};

    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    const GraphicsPipeline::Parameters createInfoTemplate{
        .cache   = nullptr,
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
        .rasterizer     = {.cullMode = CullMode::eNone},
        .viewportState  = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil   = {},
        .blending       = {.attachments = {{.blendEnable = 0u, .colorWriteMask = colorWriteMask}}},
        .multisample    = {},
        .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .pipelineLayout = *pipelineLayout,
        .renderpassDescription = {.colorAttachmentFormats = {vk::Format::eR32G32B32A32Sfloat}}};

    {
        SCOPED_TRACE("correct pipeline compiles");
        auto vertexShader = createShader(Shader::ShadingStage::eVertex, gVertexShaderString);
        auto fragmentShader =
            createShader(Shader::ShadingStage::eFragment, gFragmentShaderGoodString);

        auto createInfo    = createInfoTemplate;
        createInfo.shading = VulkanDevice->createShaderProgram(
            {.vertex   = {.shader = vertexShader, .entryPoint = "main"},
             .fragment = {.shader = fragmentShader, .entryPoint = "main"}});

        EXPECT_NO_THROW(auto pipeline = VulkanDevice->createGraphicsPipeline(createInfo););
    }
}
} // namespace VOG::Tests
