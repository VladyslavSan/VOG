#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <string>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
const std::string gVertexShaderString = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_include : enable

layout(location = 0) in vec4 inPosition;
layout(location = 3) in vec4 inSomething;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

layout( push_constant ) uniform constants
{
  int index;
  vec4 vector;
  mat4 matrix;
} PushConstants;

void main() {
    gl_Position = inPosition;
    fragColor = inColor;
}
)";

/** A vertex shader with @p inputs vec4 inputs and @p outputs vec4 outputs, both at least 1. */
std::string
vertexShaderWithAttributes(std::uint32_t inputs, std::uint32_t outputs)
{
    std::string source = "#version 450\n";
    std::string sum    = "vec4(0.0)";
    for (std::uint32_t i = 0u; i < inputs; ++i)
    {
        source += std::format("layout(location = {0}) in vec4 in{0};\n", i);
        sum += std::format(" + in{}", i);
    }
    for (std::uint32_t i = 0u; i < outputs; ++i)
    {
        source += std::format("layout(location = {0}) out vec4 out{0};\n", i);
    }
    source += "void main() {\n    gl_Position = " + sum + ";\n";
    for (std::uint32_t i = 0u; i < outputs; ++i)
    {
        source += std::format("    out{} = gl_Position;\n", i);
    }

    return source + "}\n";
}

// Resources the way a shader declares them for the descriptor heap: no set, no binding, an index
// from the push constants.
const std::string gHeapFragmentShaderString = R"(
#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

layout(descriptor_heap, descriptor_stride = 128) uniform texture2D gTextures[];
layout(descriptor_heap, descriptor_stride = 128) uniform Material { vec4 tint; } gMaterials[];
layout(descriptor_heap) uniform sampler gSamplers[];

layout(push_constant) uniform PushData
{
  mat4 mvp;
  uint material;
  uint textures[4];
} pushData;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(sampler2D(gTextures[pushData.textures[0]], gSamplers[0]), inUv) *
               gMaterials[pushData.material].tint;
}
)";

// A UBO and a sampler with a set and a binding, which pipelines without a layout cannot bind.
const std::string gSetBindingUniformShaderString = R"(
#version 450
layout(set = 0, binding = 0) uniform Scene { vec4 color; } scene;
layout(location = 0) out vec4 outColor;
void main() { outColor = scene.color; }
)";

const std::string gSetBindingSamplerShaderString = R"(
#version 450
layout(set = 0, binding = 1) uniform sampler2D gTexture;
layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(gTexture, inUv); }
)";

const std::string gSetBindingSubpassInputShaderString = R"(
#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput gInput;
layout(location = 0) out vec4 outColor;
void main() { outColor = subpassLoad(gInput); }
)";

const std::string gFragmentShaderString = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_include : enable

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(1.0, 0.0, 0.0, 1.0);
}
)";
} // namespace

/** Exactly Limits::gMaxNumStageAttributes passes; one more is a ShaderError, not a Debug abort. */
TEST_F(VulkanFixture, Shader_maxStageAttributesPassAndOneMoreThrows)
{
    constexpr std::uint32_t kMax = Limits::gMaxNumStageAttributes;

    const auto shader =
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(kMax, kMax));
    EXPECT_EQ(shader->reflection.inAttributes.size(), kMax);
    EXPECT_EQ(shader->reflection.outAttributes.size(), kMax);

    EXPECT_THROW(
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(kMax + 1u, 1u)),
        Shader::ShaderError);
    EXPECT_THROW(
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(1u, kMax + 1u)),
        Shader::ShaderError);
}

TEST_F(VulkanFixture, Shader_instantiate_vertex_shader)
{
    EXPECT_NO_THROW({
        auto        shader     = createShader(Shader::ShadingStage::eVertex, gVertexShaderString);
        const auto& reflection = shader->reflection;

        EXPECT_EQ(reflection.inAttributes.size(), 3u);
        EXPECT_EQ(reflection.inAttributes[0].location, 0u);
        EXPECT_EQ(reflection.inAttributes[0].name, "inPosition");
        EXPECT_EQ(reflection.inAttributes[1].location, 1u);
        EXPECT_EQ(reflection.inAttributes[1].name, "inColor");
        EXPECT_EQ(reflection.inAttributes[2].location, 3u);
        EXPECT_EQ(reflection.inAttributes[2].name, "inSomething");

        EXPECT_EQ(reflection.pushConstants.variables.size(), 3u);
        EXPECT_EQ(reflection.pushConstants.variables[0].name, "index");
        EXPECT_EQ(reflection.pushConstants.variables[1].name, "vector");
        EXPECT_EQ(reflection.pushConstants.variables[2].name, "matrix");
    });
}

/**
 * The design reads heap resources without reflecting them: SPIRV-Cross must accept the heap SPIR-V
 * and still report the push block, which the renderer checks against what it pushes. A SPIRV-Cross
 * that throws on the heap declarations, or drops the push block, fails here.
 */
TEST_F(VulkanFixture, Shader_heapShaderReflectsPushConstants)
{
    ShaderPtr shader;
    ASSERT_NO_THROW(shader =
                        createShader(Shader::ShadingStage::eFragment, gHeapFragmentShaderString));

    const auto& variables = shader->reflection.pushConstants.variables;
    ASSERT_EQ(variables.size(), 3u);
    EXPECT_EQ(variables[0].name, "mvp");
    EXPECT_EQ(variables[0].offset, 0u);
    EXPECT_EQ(variables[0].size, 64u);
    EXPECT_EQ(variables[1].name, "material");
    EXPECT_EQ(variables[1].offset, 64u);
    EXPECT_EQ(variables[1].size, 4u);
    EXPECT_EQ(variables[2].name, "textures");
    EXPECT_EQ(variables[2].offset, 68u);
    EXPECT_EQ(variables[2].size, 16u);
}

/** A resource with a set and a binding is rejected, whatever its kind: pipelines have no layout. */
TEST_F(VulkanFixture, Shader_rejectsDescriptorSetBindings)
{
    EXPECT_THROW(createShader(Shader::ShadingStage::eFragment, gSetBindingUniformShaderString),
                 Shader::ShaderError);
    EXPECT_THROW(createShader(Shader::ShadingStage::eFragment, gSetBindingSamplerShaderString),
                 Shader::ShaderError);
    EXPECT_THROW(createShader(Shader::ShadingStage::eFragment, gSetBindingSubpassInputShaderString),
                 Shader::ShaderError);
}

TEST_F(VulkanFixture, Shader_instantiate_fragment_shader)
{
    EXPECT_NO_THROW(
        { auto shader = createShader(Shader::ShadingStage::eFragment, gFragmentShaderString); });
}

/**
 * No Vulkan entry point is reachable through a bare shader, so the call under test is its
 * destruction (vkDestroyShaderModule on a live device).
 */
TEST_F(VulkanValidationFixture, Shader_keepsDeviceAlive)
{
    auto       shader   = createShader(Shader::ShadingStage::eFragment, gFragmentShaderString);
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, shader);
}
} // namespace VOG::Tests
