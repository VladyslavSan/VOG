#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <tuple>

#include "HeapShaderSources.hpp"
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

// A heap block of each storage class, with every kind of member: matrix, row-major matrix, array,
// struct, scalars and vectors. Each member of the uniform block is read through an expression of
// its own, and SPIR-V can make a copy of the block for each one; the buffer block is only read.
const std::string gHeapBlocksFragmentShaderString = R"(
#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

// `sampler` is a keyword of the language, so the member cannot have that name.
struct TextureRef { uint index; uint samplerIndex; };

layout(descriptor_heap, descriptor_stride = 128) uniform Material
{
    vec4 tint;
    mat3 basis;
    vec3 v3;
    float weights[3];
    float rough;
    uvec2 pair;
    TextureRef albedo;
    layout(row_major) mat4 rows;
} gMaterials[];
layout(descriptor_heap, descriptor_stride = 128) buffer Particles
{
    vec4 position;
    uint count;
} gParticles[];

layout(push_constant) uniform PushData { uint material; uint particles; } pushData;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = gMaterials[pushData.material].tint * gMaterials[pushData.material].basis[1].xyzx +
               vec4(gMaterials[pushData.material].v3, gMaterials[pushData.material].weights[2]) +
               vec4(gMaterials[pushData.material].rough,
                    float(gMaterials[pushData.material].pair.y),
                    float(gMaterials[pushData.material].albedo.index),
                    float(gParticles[pushData.particles].count)) +
               gMaterials[pushData.material].rows[0] + gParticles[pushData.particles].position;
}
)";

// Declared and never read: the block is not in the SPIR-V.
const std::string gUnreadHeapBlockFragmentShaderString = R"(
#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require

layout(descriptor_heap, descriptor_stride = 128) uniform Material { vec4 tint; } gMaterials[];

layout(location = 0) out vec4 outColor;

void main() { outColor = vec4(1.0); }
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

        EXPECT_EQ(reflection.pushConstants.size(), 3u);
        EXPECT_EQ(reflection.pushConstants[0].name, "index");
        EXPECT_EQ(reflection.pushConstants[1].name, "vector");
        EXPECT_EQ(reflection.pushConstants[2].name, "matrix");
    });
}

/**
 * SPIRV-Cross must accept the heap SPIR-V, report the push block, which the renderer checks
 * against what it pushes, and the heap block that the shader reads. A SPIRV-Cross that throws on
 * the heap declarations, or drops the push block or the heap block, fails here.
 */
TEST_F(VulkanFixture, Shader_heapShaderReflectsPushConstants)
{
    ShaderPtr shader;
    ASSERT_NO_THROW(shader =
                        createShader(Shader::ShadingStage::eFragment, gHeapFragmentShaderString));

    ASSERT_EQ(shader->reflection.heapBlocks.size(), 1u);
    const auto& block = shader->reflection.heapBlocks[0];
    EXPECT_EQ(block.name, "Material");
    EXPECT_EQ(block.storage, Shader::Reflection::HeapBlockStorage::eUniform);
    EXPECT_EQ(block.strideDecoration, 128u);
    EXPECT_EQ(block.size, 16u);
    ASSERT_EQ(block.members.size(), 1u);
    EXPECT_EQ(block.members[0].name, "tint");
    EXPECT_EQ(block.members[0].offset, 0u);
    EXPECT_EQ(block.members[0].size, 16u);
    EXPECT_EQ(block.members[0].type,
              (Shader::Reflection::BaseType{.type = Shader::Reflection::BaseType::Type::eFloat,
                                            .components = 4,
                                            .columns    = 1}));

    const auto& variables = shader->reflection.pushConstants;
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

/**
 * Every member of a heap block reflects with the layout the shader gave it, whatever the storage
 * class: std140 offsets, the stride of a matrix and of an array, the name and members of a struct.
 * The uniform block is read through eight expressions, so SPIR-V can hold a copy of it for each;
 * they reflect as one block. A buffer block reflects like a uniform one, with another storage
 * class.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(VulkanFixture, Shader_heapBlocksReflectEveryMemberAndStorageClass)
{
    using Type = Shader::Reflection::BaseType::Type;
    using Base = Shader::Reflection::BaseType;

    const auto shader =
        createShader(Shader::ShadingStage::eFragment, gHeapBlocksFragmentShaderString);

    // Sorted by name.
    ASSERT_EQ(shader->reflection.heapBlocks.size(), 2u);
    const auto& material  = shader->reflection.heapBlocks[0];
    const auto& particles = shader->reflection.heapBlocks[1];

    EXPECT_EQ(material.name, "Material");
    EXPECT_EQ(material.storage, Shader::Reflection::HeapBlockStorage::eUniform);
    EXPECT_EQ(material.strideDecoration, 128u);
    ASSERT_EQ(material.members.size(), 8u);

    const auto& tint = material.members[0];
    EXPECT_EQ(tint.name, "tint");
    EXPECT_EQ(tint.offset, 0u);
    EXPECT_EQ(tint.type, (Base{.type = Type::eFloat, .components = 4, .columns = 1}));

    const auto& basis = material.members[1];
    EXPECT_EQ(basis.name, "basis");
    EXPECT_EQ(basis.offset, 16u);
    EXPECT_EQ(basis.type, (Base{.type = Type::eFloat, .components = 3, .columns = 3}));
    EXPECT_EQ(basis.matrixStride, 16u);
    EXPECT_FALSE(basis.rowMajor);

    EXPECT_EQ(material.members[2].name, "v3");
    EXPECT_EQ(material.members[2].offset, 64u);

    const auto& weights = material.members[3];
    EXPECT_EQ(weights.name, "weights");
    EXPECT_EQ(weights.offset, 80u);
    EXPECT_EQ(weights.arraySize, 3u);
    EXPECT_EQ(weights.arrayStride, 16u);
    EXPECT_EQ(weights.type, (Base{.type = Type::eFloat, .components = 1, .columns = 1}));

    EXPECT_EQ(material.members[4].name, "rough");
    EXPECT_EQ(material.members[4].offset, 128u);
    EXPECT_EQ(material.members[5].name, "pair");
    EXPECT_EQ(material.members[5].offset, 136u);
    EXPECT_EQ(material.members[5].type, (Base{.type = Type::eUInt, .components = 2, .columns = 1}));

    const auto& albedo = material.members[6];
    EXPECT_EQ(albedo.name, "albedo");
    EXPECT_EQ(albedo.offset, 144u);
    EXPECT_EQ(albedo.structName, "TextureRef");
    ASSERT_EQ(albedo.members.size(), 2u);
    EXPECT_EQ(albedo.members[0].name, "index");
    EXPECT_EQ(albedo.members[0].offset, 0u);
    EXPECT_EQ(albedo.members[0].type, (Base{.type = Type::eUInt, .components = 1, .columns = 1}));
    EXPECT_EQ(albedo.members[1].name, "samplerIndex");
    EXPECT_EQ(albedo.members[1].offset, 4u);

    // A row-major matrix is reflected as such; the struct before it takes 16 bytes in std140.
    const auto& rows = material.members[7];
    EXPECT_EQ(rows.name, "rows");
    EXPECT_EQ(rows.offset, 160u);
    EXPECT_TRUE(rows.rowMajor);
    EXPECT_FALSE(basis.rowMajor);

    EXPECT_EQ(particles.name, "Particles");
    EXPECT_EQ(particles.storage, Shader::Reflection::HeapBlockStorage::eStorageBuffer);
    EXPECT_EQ(particles.strideDecoration, 128u);
    EXPECT_EQ(particles.size, 20u);
    ASSERT_EQ(particles.members.size(), 2u);
    EXPECT_EQ(particles.members[0].name, "position");
    EXPECT_EQ(particles.members[0].offset, 0u);
    EXPECT_EQ(particles.members[1].name, "count");
    EXPECT_EQ(particles.members[1].offset, 16u);
}

/**
 * The `descriptor_stride` of a block is reflected as the shader declares it. 0 means no stride was
 * declared, which makes the stride the size of a descriptor; the program refuses both.
 */
TEST_F(VulkanFixture, Shader_heapBlockReflectsItsStride)
{
    const auto strideOf = [this](std::string_view stride)
    {
        const auto shader = createShader(Shader::ShadingStage::eFragment,
                                         heapShader(Shader::ShadingStage::eFragment,
                                                    materialBlock("uniform", stride, "vec4 tint;"),
                                                    "gMaterials[pushData.index].tint"));
        EXPECT_EQ(shader->reflection.heapBlocks.size(), 1u);

        return shader->reflection.heapBlocks.empty()
                 ? 0xFFFFu
                 : shader->reflection.heapBlocks[0].strideDecoration;
    };

    EXPECT_EQ(strideOf("descriptor_stride = 128"), 128u);
    EXPECT_EQ(strideOf("descriptor_stride = 64"), 64u);
    EXPECT_EQ(strideOf(""), 0u);
}

/**
 * An atomic on a heap block, and the length of an unsized array in one, are accesses that the
 * reflection cannot follow, so the block would have a stride that nothing checks. Each is a
 * ShaderError, whatever the stride is, and also when plain reads of the same block sit beside it:
 * every access has a buffer pointer of its own.
 */
TEST_F(VulkanFixture, Shader_heapBlockReflectionFailsClosedForWhatItCannotFollow)
{
    const auto atomicOnly = [this](std::string_view stride)
    {
        return createShader(
            Shader::ShadingStage::eFragment,
            heapShader(
                Shader::ShadingStage::eFragment,
                std::format(
                    "layout(descriptor_heap{}) buffer Counter {{ uint count; }} gCounters[];",
                    stride.empty() ? std::string{} : std::format(", {}", stride)),
                "vec4(float(atomicAdd(gCounters[pushData.index].count, 1u)))"));
    };
    const auto lengthOnly = [this](std::string_view stride)
    {
        return createShader(
            Shader::ShadingStage::eFragment,
            heapShader(
                Shader::ShadingStage::eFragment,
                std::format("layout(descriptor_heap{}) buffer Data {{ float values[]; }} gData[];",
                            stride.empty() ? std::string{} : std::format(", {}", stride)),
                "vec4(float(gData[pushData.index].values.length()))"));
    };

    // The same accesses beside plain reads of the block: the atomic and the length() have buffer
    // pointers of their own, which no plain read claims.
    const auto atomicAndRead = [this](std::string_view stride)
    {
        return createShader(Shader::ShadingStage::eFragment,
                            std::format(R"(#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
layout(descriptor_heap, descriptor_stride = {}) buffer Foo {{ vec4 a; uint n; }} gFoo[];
layout(push_constant) uniform P {{ uint i; }} p;
layout(location = 0) out vec4 outColor;
void main() {{
    uint c = atomicAdd(gFoo[p.i].n, 1u);
    outColor = gFoo[p.i].a * float(c);
}}
)",
                                        stride));
    };
    const auto lengthLoop = [this](std::string_view stride)
    {
        return createShader(Shader::ShadingStage::eFragment,
                            std::format(R"(#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
layout(descriptor_heap, descriptor_stride = {}) buffer Data {{ float values[]; }} data[];
layout(push_constant) uniform P {{ uint i; }} p;
layout(location = 0) out vec4 outColor;
void main() {{
    float s = 0.0;
    for (int j = 0; j < data[p.i].values.length(); ++j)
    {{
        s += data[p.i].values[j];
    }}
    outColor = vec4(s);
}}
)",
                                        stride));
    };

    for (const auto* stride : {"64", "128"})
    {
        EXPECT_THROW(std::ignore = atomicAndRead(stride), Shader::ShaderError) << stride;
        EXPECT_THROW(std::ignore = lengthLoop(stride), Shader::ShaderError) << stride;
    }
    for (const auto* stride : {"descriptor_stride = 64", "descriptor_stride = 128"})
    {
        EXPECT_THROW(std::ignore = atomicOnly(stride), Shader::ShaderError) << stride;
        EXPECT_THROW(std::ignore = lengthOnly(stride), Shader::ShaderError) << stride;
    }
}

/**
 * A member of a block that the reflection has no name for is a ShaderError that names the block
 * and the member, in a heap block and in the push block alike: a 64-bit integer, and a pointer
 * (buffer_reference). A pointer to a struct that points to itself would otherwise be followed for
 * ever. It is tested in the push block: in a heap block, SPIRV-Cross itself crashes in its
 * constructor once the shader dereferences such a pointer (measured), before this code runs.
 */
TEST_F(VulkanFixture, Shader_blockMemberOfAnUnreflectableTypeIsAShaderError)
{
    constexpr std::string_view kInt64 =
        "#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require\n";

    const auto expectError =
        [this](const std::string& source, std::string_view block, std::string_view member)
    {
        try
        {
            std::ignore = createShader(Shader::ShadingStage::eFragment, source);
            ADD_FAILURE() << "No ShaderError for " << block << "." << member;
        }
        catch (const Shader::ShaderError& error)
        {
            EXPECT_NE(std::string_view{error.what()}.find(std::format("\"{}\"", block)),
                      std::string_view::npos)
                << error.what();
            EXPECT_NE(std::string_view{error.what()}.find(std::format("\"{}\"", member)),
                      std::string_view::npos)
                << error.what();
        }
    };

    expectError(
        heapShader(Shader::ShadingStage::eFragment,
                   std::format("{}{}",
                               kInt64,
                               materialBlock("uniform", "descriptor_stride = 128", "int64_t big;")),
                   "vec4(float(gMaterials[pushData.index].big))"),
        "Material",
        "big");

    constexpr std::string_view kLeaf =
        "#extension GL_EXT_buffer_reference : require\n"
        "layout(buffer_reference, std430) buffer Leaf { uint value; };\n";
    expectError(
        heapShader(
            Shader::ShadingStage::eFragment,
            std::format("{}{}",
                        kLeaf,
                        materialBlock("uniform", "descriptor_stride = 128", "Leaf head; vec4 v;")),
            "gMaterials[pushData.index].v + vec4(gMaterials[pushData.index].head.value)"),
        "Material",
        "head");

    // The push block takes the same path. Here the pointer is to a struct that points to itself:
    // without the refusal the reflection would follow Node.next for ever.
    expectError(std::format(R"(#version 450
{}
layout(push_constant) uniform PC {{ int64_t big; }} pc;
layout(location = 0) out vec4 outColor;
void main() {{ outColor = vec4(float(pc.big)); }}
)",
                            kInt64),
                "PC",
                "big");
    expectError(R"(#version 450
#extension GL_EXT_buffer_reference : require
layout(buffer_reference) buffer Node;
layout(buffer_reference, std430) buffer Node { Node next; uint value; };
layout(push_constant) uniform PC { Node head; } pc;
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(pc.head.next.value); }
)",
                "PC",
                "head");
}

/** A block that no instruction reaches is not in the SPIR-V, so it is not reflected. */
TEST_F(VulkanFixture, Shader_unreadHeapBlockIsNotReflected)
{
    const auto shader =
        createShader(Shader::ShadingStage::eFragment, gUnreadHeapBlockFragmentShaderString);

    EXPECT_TRUE(shader->reflection.heapBlocks.empty());
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
