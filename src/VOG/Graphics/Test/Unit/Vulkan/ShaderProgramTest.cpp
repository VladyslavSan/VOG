#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/GLSLCompiler.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{

const std::string gMinimalVertexShader = R"(
#version 450
layout(location = 0) in vec4 inPosition;
void main() { gl_Position = inPosition; }
)";

const std::string gMinimalFragmentShader = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0); }
)";

const std::string gVertexShaderPushConstants = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { vec4 transform; } pc;
void main() { gl_Position = inPosition + pc.transform; }
)";

GraphicsPipeline::Parameters
makeMinimalPipelineParameters(ShaderProgramPtr program)
{
    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    return GraphicsPipeline::Parameters{
        .shading       = program,
        .vertexLayout  = {.bindingDescription   = {{.binding   = 0u,
                                                    .stride    = 16u,
                                                    .inputRate = vk::VertexInputRate::eVertex}},
                          .attributeDescription = {{.location = 0u,
                                                    .binding  = 0u,
                                                    .format   = vk::Format::eR32G32B32A32Sfloat,
                                                    .offset   = 0u}}},
        .rasterizer    = {.cullMode = CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {},
        .blending      = {.attachments = {{.colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {vk::Format::eR32G32B32A32Sfloat}},
    };
}

} // namespace

// ============================================================
// Structural tests (VulkanFixture)
// ============================================================

TEST_F(VulkanFixture, ShaderProgram_create)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    EXPECT_NO_THROW(
        ShaderProgram::create({.vertex = {.shader = vert}, .fragment = {.shader = frag}}));
}

TEST_F(VulkanFixture, ShaderProgram_pushConstants_reflectedCorrectly)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderPushConstants);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = ShaderProgram::create(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    ASSERT_EQ(program->pushConstants.ranges.size(), 1u);
    EXPECT_EQ(program->pushConstants.ranges[0].offset, 0u);
    EXPECT_EQ(program->pushConstants.ranges[0].size, 16u);
    EXPECT_EQ(program->pushConstants.names[0], "transform");
}

// ============================================================
// Lifetime tests (VulkanFixture)
// ============================================================

/**
 * The pipeline retains the device through its DeviceChild base, which is destroyed after the
 * pipeline handle. Its ShaderProgram member also holds the device, through the shaders, but is
 * destroyed before the handle, so only the base keeps the device alive until vkDestroyPipeline:
 * without it the layer reports the pipeline as still alive at vkDestroyDevice.
 */
TEST_F(VulkanValidationFixture, GraphicsPipeline_keepsDeviceAlive)
{
    ShaderProgramPtr program = ShaderProgram::create(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    GraphicsPipelinePtr pipeline =
        mDevice->createGraphicsPipeline(makeMinimalPipelineParameters(program));
    program.reset();
    const auto released = releaseDevice();

    // Everything made inside is gone when the lambda returns, so the pipeline is the last holder.
    expectDependentHoldsDevice(
        released,
        pipeline,
        [&pipeline](GraphicsPipeline&)
        {
            const auto&                   device = pipeline->device();
            auto                          pool   = device->createCommandBufferPool();
            std::shared_ptr<RenderBuffer> target =
                device->createRenderBuffer(AttachmentUsage::eColor,
                                           vk::Format::eR32G32B32A32Sfloat,
                                           {.width = 16u, .height = 16u},
                                           SampleCount::e1,
                                           1u,
                                           1u,
                                           vk::ImageTiling::eOptimal,
                                           vk::ImageLayout::eUndefined,
                                           vk::MemoryPropertyFlagBits::eDeviceLocal);
            auto recorder = pool->record();

            recorder.setImageBarrier(target, toColorAttachmentBarrier());
            {
                auto pass = recorder.beginRendering({{.attachment = target}});
                pass.bindPipeline(pipeline);
            }
            auto executable = recorder.finish();
        });
}

/**
 * The command buffer retains a pipeline once, however often it binds it: retention is a set, so
 * a draw-heavy frame does not grow the command buffer's retained list.
 */
TEST_F(VulkanFixture, CommandBufferRecorder_bindingOnePipelineRepeatedlyRetainsItOnce)
{
    ShaderProgramPtr program = ShaderProgram::create(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    GraphicsPipelinePtr pipeline =
        mDevice->createGraphicsPipeline(makeMinimalPipelineParameters(program));
    std::shared_ptr<RenderBuffer> target =
        mDevice->createRenderBuffer(AttachmentUsage::eColor,
                                    vk::Format::eR32G32B32A32Sfloat,
                                    {.width = 16u, .height = 16u},
                                    SampleCount::e1,
                                    1u,
                                    1u,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageLayout::eUndefined,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto pool     = mDevice->createCommandBufferPool();
    auto recorder = pool->record();
    {
        auto pass = recorder.beginRendering({{.attachment = target}});
        for (int i = 0; i < 1000; ++i)
        {
            pass.bindPipeline(pipeline);
        }
    }

    // One reference for the test, one for the command buffer.
    EXPECT_EQ(pipeline.use_count(), 2);
}

} // namespace VOG::Tests