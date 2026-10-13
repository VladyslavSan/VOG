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

const std::string gVertexShaderSets01 = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(set = 0, binding = 0) uniform UBO0 { vec4 color; } ubo0;
layout(set = 1, binding = 0) uniform UBO1 { vec4 color; } ubo1;
void main() { gl_Position = ubo0.color + ubo1.color + inPosition; }
)";

// Sets 0 and 2 are used; set 1 is a gap.
// makePipelineLayout must insert an empty layout at index 1 so that
// Vulkan sees a contiguous pSetLayouts array where index 2 maps to set 2.
const std::string gVertexShaderSets02Gap = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(set = 0, binding = 0) uniform UBO0 { vec4 color; } ubo0;
layout(set = 2, binding = 0) uniform UBO2 { vec4 color; } ubo2;
void main() { gl_Position = ubo0.color + ubo2.color + inPosition; }
)";

const std::string gVertexShaderPushConstants = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { vec4 transform; } pc;
void main() { gl_Position = inPosition + pc.transform; }
)";

GraphicsPipeline::Parameters
makeMinimalPipelineParameters(ShaderProgramPtr program, const vk::PipelineLayout& layout)
{
    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    return GraphicsPipeline::Parameters{
        .cache          = nullptr,
        .shading        = program,
        .vertexLayout   = {.bindingDescription   = {{.binding   = 0u,
                                                     .stride    = 16u,
                                                     .inputRate = vk::VertexInputRate::eVertex}},
                           .attributeDescription = {{.location = 0u,
                                                     .binding  = 0u,
                                                     .format   = vk::Format::eR32G32B32A32Sfloat,
                                                     .offset   = 0u}}},
        .rasterizer     = {.cullMode = CullMode::eNone},
        .viewportState  = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil   = {},
        .blending       = {.attachments = {{.colorWriteMask = colorWriteMask}}},
        .multisample    = {},
        .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .pipelineLayout = layout,
        .renderingDescription = {.colorAttachmentFormats = {vk::Format::eR32G32B32A32Sfloat}},
    };
}

} // namespace

// ============================================================
// Structural tests (VulkanFixture)
// ============================================================

TEST_F(VulkanFixture, ShaderProgram_create_noDescriptorSets)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    EXPECT_NO_THROW(
        mDevice->createShaderProgram({.vertex = {.shader = vert}, .fragment = {.shader = frag}}));
}

TEST_F(VulkanFixture, ShaderProgram_descriptorSets_consecutiveSets)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderSets01);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = mDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    EXPECT_TRUE(static_cast<bool>(*program->descriptorSets[0].setLayout));
    EXPECT_TRUE(static_cast<bool>(*program->descriptorSets[1].setLayout));
    EXPECT_FALSE(static_cast<bool>(*program->descriptorSets[2].setLayout));
    EXPECT_FALSE(static_cast<bool>(*program->descriptorSets[3].setLayout));
}

TEST_F(VulkanFixture, ShaderProgram_descriptorSets_gapInSets)
{
    // buildDescriptorSets leaves set 1 null because no binding is declared for it.
    // makePipelineLayout is responsible for filling the gap with an empty layout.
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderSets02Gap);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = mDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    EXPECT_TRUE(static_cast<bool>(*program->descriptorSets[0].setLayout));
    EXPECT_FALSE(static_cast<bool>(*program->descriptorSets[1].setLayout)); // gap — no binding here
    EXPECT_TRUE(static_cast<bool>(*program->descriptorSets[2].setLayout));
    EXPECT_FALSE(static_cast<bool>(*program->descriptorSets[3].setLayout));
}

TEST_F(VulkanFixture, ShaderProgram_pushConstants_reflectedCorrectly)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderPushConstants);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = mDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    ASSERT_EQ(program->pushConstants.ranges.size(), 1u);
    EXPECT_EQ(program->pushConstants.ranges[0].stageFlags, vk::ShaderStageFlagBits::eVertex);
    EXPECT_EQ(program->pushConstants.names[0], "transform");
}

// ============================================================
// Lifetime tests (VulkanFixture)
// ============================================================

/**
 * The program retains its shaders and the device. No Vulkan entry point is reachable through a
 * bare program, so the call under test is its destruction (vkDestroyPipelineLayout).
 */
TEST_F(VulkanValidationFixture, ShaderProgram_keepsDeviceAlive)
{
    auto program = mDevice->createShaderProgram(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, program);
}

/**
 * The pipeline retains the device through its DeviceChild base, which is destroyed after the
 * pipeline handle. Its ShaderProgram member also holds the device, but is destroyed before the
 * handle, so only the base keeps the device alive until vkDestroyPipeline: without it the layer
 * reports the pipeline as still alive at vkDestroyDevice.
 */
TEST_F(VulkanValidationFixture, GraphicsPipeline_keepsDeviceAlive)
{
    ShaderProgramPtr program = mDevice->createShaderProgram(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    GraphicsPipelinePtr pipeline = mDevice->createGraphicsPipeline(
        makeMinimalPipelineParameters(program, *program->pipelineLayout));
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
            auto                  commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
            CommandBufferRecorder recorder{**commandBuffer};

            recorder.setImageBarrier(target, toColorAttachmentBarrier(target->getImage()));
            {
                auto pass = recorder.beginRendering({{.attachment = target}});
                pass.bindPipeline(pipeline);
            }
            recorder.finish();
        });
}

// ============================================================
// Gap-filling regression test (VulkanValidationFixture)
//
// Verifies that makePipelineLayout inserts an empty descriptor set layout
// at index 1 when sets 0 and 2 are used but set 1 is absent, keeping
// pSetLayouts contiguous so Vulkan can resolve set 2 to the correct index.
//
// The validation layer detects a mismatch at vkCreateGraphicsPipelines if
// the pipeline layout does not cover all statically-used descriptor sets.
// ============================================================

TEST_F(VulkanValidationFixture, ShaderProgram_makePipelineLayout_gapFilledCorrectly)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderSets02Gap);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> shaderProgram;
    ASSERT_NO_THROW(shaderProgram = mDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    ASSERT_NO_THROW({
        auto pipeline = mDevice->createGraphicsPipeline(
            makeMinimalPipelineParameters(shaderProgram, *shaderProgram->pipelineLayout));
    });

    EXPECT_TRUE(mValidationErrors.empty())
        << "Validation error (gap likely not filled with empty layout):\n"
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

} // namespace VOG::Tests