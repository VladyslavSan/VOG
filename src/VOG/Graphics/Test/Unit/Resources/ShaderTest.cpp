#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include "GraphicsApiFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests::Shader
{
namespace
{
const std::string gVertexShaderString = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_include : enable

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

layout(set = 2, binding = 3) uniform SomeUniform
{
  vec4 someVector;
  int someInt;
} u_UniformBuffer;

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

TEST_F(GraphicsProviderFixture, Shader_instantiate_vertex_shader)
{
    EXPECT_NO_THROW({
        auto shader = VOG::Graphics::Vulkan::Shader::create(
            mGraphicsProvider->getDevice(), ShadingStage::eVertex, gVertexShaderString);
        auto reflection = shader->reflect();

        EXPECT_EQ(reflection.uniformBuffers.size(), 1u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.set, 2u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.binding, 3u);

        EXPECT_EQ(reflection.pushConstants.variables.size(), 3u);
        EXPECT_EQ(reflection.pushConstants.variables[0].name, "index");
        EXPECT_EQ(reflection.pushConstants.variables[1].name, "vector");
        EXPECT_EQ(reflection.pushConstants.variables[2].name, "matrix");
    });
}

TEST_F(GraphicsProviderFixture, Shader_instantiate_fragment_shader)
{
    EXPECT_NO_THROW({
        auto shader = VOG::Graphics::Vulkan::Shader::create(
            mGraphicsProvider->getDevice(), ShadingStage::eFragment, gFragmentShaderString);
    });
}
} // namespace VOG::Tests::Shader
