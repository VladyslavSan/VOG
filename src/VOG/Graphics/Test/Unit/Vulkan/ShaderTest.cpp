#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>

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

/** A fragment shader with @p inputs vec4 inputs and @p outputs vec4 outputs, the latter at least 1.
 */
std::string
fragmentShaderWithAttributes(std::uint32_t inputs, std::uint32_t outputs)
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
    source += "void main() {\n";
    for (std::uint32_t i = 0u; i < outputs; ++i)
    {
        source += std::format("    out{} = {};\n", i, sum);
    }

    return source + "}\n";
}

// The same interface block as the output of a vertex shader and the input of a fragment shader.
// Its first member takes the location 0 that glslang gives it, its second member location 3.
const std::string gBlockVertexShaderString = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(location = 0) out VertexOut { vec2 uv; layout(location = 3) vec4 color; } vertexOut;
void main() { gl_Position = inPosition; vertexOut.uv = inPosition.xy; vertexOut.color = inPosition; }
)";

const std::string gBlockFragmentShaderString = R"(
#version 450
layout(location = 0) in VertexOut { vec2 uv; layout(location = 3) vec4 color; } vertexIn;
layout(location = 0) out vec4 outColor;
void main() { outColor = vertexIn.color + vec4(vertexIn.uv, 0.0, 0.0); }
)";

// A block at location 2: the members after the first follow the locations that the one before
// takes (a mat3 takes three, a vec4[2] two).
const std::string gMultiLocationBlockFragmentShaderString = R"(
#version 450
layout(location = 2) in Packed { vec2 first; mat3 matrix; vec4 array[2]; float last; } packed;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(packed.first, packed.matrix[2][2], packed.array[1].x + packed.last); }
)";

// A block of 64-bit members: a dvec3 takes two locations, a dmat2x3 two columns of two.
const std::string gWideBlockVertexShaderString = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(location = 1) out W { flat dvec3 a; flat dmat2x3 b; float c; } w;
void main() {
    gl_Position = inPosition;
    w.a = dvec3(inPosition.xyz);
    w.b = dmat2x3(1.0);
    w.c = 1.0;
}
)";

// A struct variable that is no interface block: its members take the locations after its own.
const std::string gStructVariableFragmentShaderString = R"(
#version 450
struct S { vec2 a; vec4 b; };
layout(location = 1) in S s;
layout(location = 0) out vec4 outColor;
void main() { outColor = s.b + vec4(s.a, 0.0, 0.0); }
)";

const std::string gBlockArrayFragmentShaderString = R"(
#version 450
layout(location = 1) in B { vec2 a; vec4 b; } blocks[2];
layout(location = 0) out vec4 outColor;
void main() { outColor = blocks[0].b + blocks[1].b + vec4(blocks[0].a, 0.0, 0.0); }
)";

const std::string gNestedStructBlockFragmentShaderString = R"(
#version 450
struct Inner { vec2 value; };
layout(location = 0) in Outer { Inner inner; } outer;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(outer.inner.value, 0.0, 1.0); }
)";

// 64-bit vectors of three and four components take two locations each; one of two takes one.
const std::string gWideInputsVertexShaderString = R"(
#version 450
layout(location = 0) in dvec3 inThree;
layout(location = 2) in dvec4 inFour;
layout(location = 4) in dvec2 inTwo;
void main() { gl_Position = vec4(inThree.x + inFour.w, inTwo.y, 0.0, 1.0); }
)";

// An array whose size is a specialization constant, default 2: two locations, not the ID of the
// constant.
const std::string gSpecializedArrayVertexShaderString = R"(
#version 450
layout(constant_id = 0) const int kCount = 2;
layout(location = 0) in vec4 inColors[kCount];
layout(location = 2) in vec4 inPosition;
void main() { gl_Position = inPosition + inColors[0] + inColors[1]; }
)";

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

/**
 * A pipeline takes at most Limits::gMaxNumVertexInputs vertex inputs: exactly that many pass and
 * one more is a ShaderError, not a Debug abort. The cap is about the vertex buffers, so the outputs
 * of the vertex stage and the inputs and outputs of the fragment stage have none.
 */
TEST_F(VulkanFixture, Shader_maxVertexInputsPassAndOneMoreThrows)
{
    constexpr std::uint32_t kMax = Limits::gMaxNumVertexInputs;

    const auto shader =
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(kMax, kMax));
    EXPECT_EQ(shader->reflection.inAttributes.size(), kMax);
    EXPECT_EQ(shader->reflection.outAttributes.size(), kMax);

    EXPECT_THROW(
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(kMax + 1u, 1u)),
        Shader::ShaderError);

    const auto wide =
        createShader(Shader::ShadingStage::eVertex, vertexShaderWithAttributes(1u, kMax + 1u));
    EXPECT_EQ(wide->reflection.outAttributes.size(), kMax + 1u);
    const auto fragment = createShader(Shader::ShadingStage::eFragment,
                                       fragmentShaderWithAttributes(kMax + 1u, kMax + 1u));
    EXPECT_EQ(fragment->reflection.inAttributes.size(), kMax + 1u);
    EXPECT_EQ(fragment->reflection.outAttributes.size(), kMax + 1u);
}

/**
 * An interface block reflects as one attribute per member, named "Block.member", in both stages: a
 * member at the location of its own decoration, or else after the member before it, which can take
 * more than one location. A struct inside the block has no location to report and is a
 * ShaderError that names the block and the member.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(VulkanFixture, Shader_interfaceBlockReflectsOneAttributePerMember)
{
    const auto vertex   = createShader(Shader::ShadingStage::eVertex, gBlockVertexShaderString);
    const auto fragment = createShader(Shader::ShadingStage::eFragment, gBlockFragmentShaderString);
    for (const auto* attributes :
         {&vertex->reflection.outAttributes, &fragment->reflection.inAttributes})
    {
        ASSERT_EQ(attributes->size(), 2u);
        EXPECT_EQ((*attributes)[0].name, "VertexOut.uv");
        EXPECT_EQ((*attributes)[0].location, 0u);
        EXPECT_EQ((*attributes)[0].format.components, 2u);
        EXPECT_EQ((*attributes)[1].name, "VertexOut.color");
        EXPECT_EQ((*attributes)[1].location, 3u);
        EXPECT_EQ((*attributes)[1].format.components, 4u);
    }

    const auto packed =
        createShader(Shader::ShadingStage::eFragment, gMultiLocationBlockFragmentShaderString);
    const auto& members = packed->reflection.inAttributes;
    ASSERT_EQ(members.size(), 4u);
    EXPECT_EQ(members[0].name, "Packed.first");
    EXPECT_EQ(members[0].location, 2u);
    EXPECT_EQ(members[0].locationCount, 1u);
    EXPECT_EQ(members[1].name, "Packed.matrix");
    EXPECT_EQ(members[1].location, 3u);
    EXPECT_EQ(members[1].locationCount, 3u);
    EXPECT_EQ(members[2].name, "Packed.array");
    EXPECT_EQ(members[2].location, 6u);
    EXPECT_EQ(members[2].locationCount, 2u);
    EXPECT_EQ(members[3].name, "Packed.last");
    EXPECT_EQ(members[3].location, 8u);
    EXPECT_EQ(members[3].locationCount, 1u);

    // A 64-bit vec3 takes two locations and a dmat2x3 two columns of two; the float after them is
    // at 1 + 2 + 4.
    const auto  wide    = createShader(Shader::ShadingStage::eVertex, gWideBlockVertexShaderString);
    const auto& wideOut = wide->reflection.outAttributes;
    ASSERT_EQ(wideOut.size(), 3u);
    EXPECT_EQ(wideOut[0].name, "W.a");
    EXPECT_EQ(wideOut[0].location, 1u);
    EXPECT_EQ(wideOut[0].locationCount, 2u);
    EXPECT_EQ(wideOut[1].name, "W.b");
    EXPECT_EQ(wideOut[1].location, 3u);
    EXPECT_EQ(wideOut[1].locationCount, 4u);
    EXPECT_EQ(wideOut[2].name, "W.c");
    EXPECT_EQ(wideOut[2].location, 7u);
    EXPECT_EQ(wideOut[2].locationCount, 1u);

    // A struct variable that is no block takes the same path, named after the variable.
    const auto structVariable =
        createShader(Shader::ShadingStage::eFragment, gStructVariableFragmentShaderString);
    const auto& structMembers = structVariable->reflection.inAttributes;
    ASSERT_EQ(structMembers.size(), 2u);
    EXPECT_EQ(structMembers[0].name, "s.a");
    EXPECT_EQ(structMembers[0].location, 1u);
    EXPECT_EQ(structMembers[1].name, "s.b");
    EXPECT_EQ(structMembers[1].location, 2u);

    // What has no single location per member is refused, never given a wrong one.
    try
    {
        std::ignore =
            createShader(Shader::ShadingStage::eFragment, gNestedStructBlockFragmentShaderString);
        ADD_FAILURE() << "A struct in an interface block did not throw.";
    }
    catch (const Shader::ShaderError& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("Outer"), std::string_view::npos);
        EXPECT_NE(std::string_view{error.what()}.find("inner"), std::string_view::npos);
    }
    EXPECT_THROW(createShader(Shader::ShadingStage::eFragment, gBlockArrayFragmentShaderString),
                 Shader::ShaderError);
}

/**
 * The size of an array of attributes can be a specialization constant; its default counts. A 64-bit
 * vector of three or four components takes two locations, one of two components takes one.
 */
TEST_F(VulkanFixture, Shader_locationCountCoversArraysAndWideVectors)
{
    const auto shader =
        createShader(Shader::ShadingStage::eVertex, gSpecializedArrayVertexShaderString);
    const auto& inputs = shader->reflection.inAttributes;

    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(inputs[0].name, "inColors");
    EXPECT_EQ(inputs[0].locationCount, 2u);
    EXPECT_EQ(inputs[1].name, "inPosition");
    EXPECT_EQ(inputs[1].locationCount, 1u);

    const auto  wide = createShader(Shader::ShadingStage::eVertex, gWideInputsVertexShaderString);
    const auto& wideInputs = wide->reflection.inAttributes;
    ASSERT_EQ(wideInputs.size(), 3u);
    EXPECT_EQ(wideInputs[0].name, "inThree");
    EXPECT_EQ(wideInputs[0].locationCount, 2u);
    EXPECT_EQ(wideInputs[1].name, "inFour");
    EXPECT_EQ(wideInputs[1].locationCount, 2u);
    EXPECT_EQ(wideInputs[2].name, "inTwo");
    EXPECT_EQ(wideInputs[2].locationCount, 1u);
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
