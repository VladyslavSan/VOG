#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
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

    auto renderPass =
        RenderPass::create(VulkanDevice,
                           {{.format        = vk::Format::eR32G32B32A32Sfloat,
                             .loadOp        = vk::AttachmentLoadOp::eClear,
                             .storeOp       = vk::AttachmentStoreOp::eStore,
                             .initialLayout = vk::ImageLayout::eUndefined,
                             .finalLayout   = vk::ImageLayout::eColorAttachmentOptimal}},
                           {});

    vk::raii::PipelineLayout pipelineLayout{*VulkanDevice, vk::PipelineLayoutCreateInfo{}};

    const GraphicsPipeline::CreateInfo createInfoTemplate{
        .device  = VulkanDevice,
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
        .blending       = {.attachments = {{.blendEnable    = 0u,
                                            .colorWriteMask = ColorComponent::eR | ColorComponent::eG |
                                                              ColorComponent::eB | ColorComponent::eA}}},
        .multisample    = {},
        .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .pipelineLayout = *pipelineLayout,
        .renderPass     = *renderPass};

    {
        SCOPED_TRACE("correct pipeline compiles");
        auto vertexShader =
            Shader::create(VulkanDevice, Shader::ShadingStage::eVertex, gVertexShaderString);
        auto fragmentShader = Shader::create(
            VulkanDevice, Shader::ShadingStage::eFragment, gFragmentShaderGoodString);

        auto createInfo = createInfoTemplate;
        createInfo.shading =
            ShaderProgram::create({.vertex   = {.shader = vertexShader, .entryPoint = "main"},
                                   .fragment = {.shader = fragmentShader, .entryPoint = "main"}});

        EXPECT_NO_THROW(auto pipeline = std::make_shared<GraphicsPipeline>(createInfo););
    }
}
} // namespace VOG::Tests
