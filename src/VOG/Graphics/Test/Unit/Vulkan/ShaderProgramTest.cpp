#include <VOG/Graphics/Vulkan/GLSLCompiler.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include "VulkanFixture.hpp"

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

GraphicsPipeline::ParametersLegacy
makeMinimalPipelineParameters(ShaderProgramPtr          program,
                              const RenderPass&         renderPass,
                              const vk::PipelineLayout& layout)
{
    return GraphicsPipeline::ParametersLegacy{
        .cache         = nullptr,
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
        .blending = {.attachments = {{.colorWriteMask = ColorComponent::eR | ColorComponent::eG |
                                                        ColorComponent::eB | ColorComponent::eA}}},
        .multisample    = {},
        .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .pipelineLayout = layout,
        .renderPass     = renderPass,
        .subpass        = 0u,
    };
}

} // namespace

// ============================================================
// Validation fixture — enables VK_LAYER_KHRONOS_validation and
// records ERROR-severity validation messages into validationErrors.
// Tests skip if the layer or extension is not installed.
// ============================================================

class VulkanValidationFixture : public testing::Test
{
protected:
    static vk::Bool32
    onMessage(vk::DebugUtilsMessageSeverityFlagBitsEXT /*severity*/,
              vk::DebugUtilsMessageTypeFlagsEXT /*types*/,
              const vk::DebugUtilsMessengerCallbackDataEXT* pData,
              void*                                         pUserData)
    {
        if (pData->pMessage)
            static_cast<std::vector<std::string>*>(pUserData)->emplace_back(pData->pMessage);
        return VK_FALSE;
    }

    static bool
    prerequisitesAvailable()
    {
        vk::raii::Context ctx;
        bool              hasLayer = false;
        bool              hasExt   = false;
        for (const auto& l : ctx.enumerateInstanceLayerProperties())
            if (std::string_view{l.layerName} == "VK_LAYER_KHRONOS_validation")
            {
                hasLayer = true;
                break;
            }
        for (const auto& e : ctx.enumerateInstanceExtensionProperties())
            if (std::string_view{e.extensionName} == "VK_EXT_debug_utils")
            {
                hasExt = true;
                break;
            }
        return hasLayer && hasExt;
    }

    VulkanValidationFixture()
    {
        if (!prerequisitesAvailable())
            return;

        VulkanInstance = Graphics::Vulkan::Instance::create({
            .appName    = "VOG Validation Test",
            .engineName = "",
            .layers     = {"VK_LAYER_KHRONOS_validation"},
            .extensions = {"VK_EXT_debug_utils"},
        });

        messenger =
            VulkanInstance->createDebugUtilsMessengerEXT(vk::DebugUtilsMessengerCreateInfoEXT{
                .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                .messageType     = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                .pfnUserCallback = VulkanValidationFixture::onMessage,
                .pUserData       = &validationErrors,
            });

        VulkanDevice = VulkanInstance->makeDevice();
    }

    Graphics::Vulkan::ShaderPtr
    createShader(Graphics::Vulkan::Shader::ShadingStage stage, const std::string& glslCode)
    {
        return VulkanDevice->createShader(stage, Graphics::Vulkan::compileGLSL(stage, glslCode));
    }

    Graphics::Vulkan::InstancePtr    VulkanInstance;
    vk::raii::DebugUtilsMessengerEXT messenger{nullptr};
    Graphics::Vulkan::DevicePtr      VulkanDevice;
    std::vector<std::string>         validationErrors;
};

// ============================================================
// Structural tests (VulkanFixture)
// ============================================================

TEST_F(VulkanFixture, ShaderProgram_create_noDescriptorSets)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    EXPECT_NO_THROW(VulkanDevice->createShaderProgram(
        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));
}

TEST_F(VulkanFixture, ShaderProgram_descriptorSets_consecutiveSets)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderSets01);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = VulkanDevice->createShaderProgram(
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
    ASSERT_NO_THROW(program = VulkanDevice->createShaderProgram(
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
    ASSERT_NO_THROW(program = VulkanDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    ASSERT_EQ(program->pushConstants.ranges.size(), 1u);
    EXPECT_EQ(program->pushConstants.ranges[0].stageFlags, vk::ShaderStageFlagBits::eVertex);
    EXPECT_EQ(program->pushConstants.names[0], "transform");
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
    if (!VulkanDevice)
        GTEST_SKIP() << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils not available";

    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderSets02Gap);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> shaderProgram;
    ASSERT_NO_THROW(shaderProgram = VulkanDevice->createShaderProgram(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    auto renderPass =
        VulkanDevice->createRenderPass({{.format      = vk::Format::eR32G32B32A32Sfloat,
                                         .loadOp      = vk::AttachmentLoadOp::eClear,
                                         .storeOp     = vk::AttachmentStoreOp::eStore,
                                         .finalLayout = vk::ImageLayout::eColorAttachmentOptimal}},
                                       {});

    ASSERT_NO_THROW({
        auto pipeline = VulkanDevice->createGraphicsPipeline(makeMinimalPipelineParameters(
            shaderProgram, *renderPass, *shaderProgram->pipelineLayout));
    });

    EXPECT_TRUE(validationErrors.empty())
        << "Validation error (gap likely not filled with empty layout):\n"
        << (validationErrors.empty() ? "" : validationErrors.front());
}

} // namespace VOG::Tests