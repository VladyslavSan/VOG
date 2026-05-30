#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <gtest/gtest.h>

#include "VulkanFixture.hpp"

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

layout(set = 2, binding = 3) uniform SomeUniform
{
  vec4 someVector;
  int someInt;
} u_UniformBuffer;

layout(set = 3, binding = 2) uniform SomeUniform2
{
  vec4 someVector;
  int someInt;
} u_UniformBuffer2;

layout(set = 3, binding = 1) uniform SomeUniform3
{
  vec4 someVector;
  int someInt;
} u_UniformBuffer3;

layout(set = 4, binding = 0) uniform sampler1D textures1D[];
layout(set = 4, binding = 0) uniform sampler2D textures2D[];
layout(set = 4, binding = 0) uniform sampler3D textures3D[];

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

TEST_F(VulkanFixture, Shader_instantiate_vertex_shader)
{
    EXPECT_NO_THROW({
        auto shader =
            Shader::create(VulkanDevice, Shader::ShadingStage::eVertex, gVertexShaderString);
        const auto& reflection = shader->reflection;

        EXPECT_EQ(reflection.inAttributes.size(), 3u);
        EXPECT_EQ(reflection.inAttributes[0].location, 0u);
        EXPECT_EQ(reflection.inAttributes[0].name, "inPosition");
        EXPECT_EQ(reflection.inAttributes[1].location, 1u);
        EXPECT_EQ(reflection.inAttributes[1].name, "inColor");
        EXPECT_EQ(reflection.inAttributes[2].location, 3u);
        EXPECT_EQ(reflection.inAttributes[2].name, "inSomething");

        EXPECT_EQ(reflection.uniformBuffers.size(), 3u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.set, 2u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.binding, 3u);
        EXPECT_EQ(reflection.uniformBuffers[1].location.set, 3u);
        EXPECT_EQ(reflection.uniformBuffers[1].location.binding, 1u);
        EXPECT_EQ(reflection.uniformBuffers[2].location.set, 3u);
        EXPECT_EQ(reflection.uniformBuffers[2].location.binding, 2u);

        EXPECT_EQ(reflection.pushConstants.variables.size(), 3u);
        EXPECT_EQ(reflection.pushConstants.variables[0].name, "index");
        EXPECT_EQ(reflection.pushConstants.variables[1].name, "vector");
        EXPECT_EQ(reflection.pushConstants.variables[2].name, "matrix");
    });
}

TEST_F(VulkanFixture, Shader_instantiate_fragment_shader)
{
    EXPECT_NO_THROW({
        auto shader =
            Shader::create(VulkanDevice, Shader::ShadingStage::eFragment, gFragmentShaderString);
    });
}
} // namespace VOG::Tests
