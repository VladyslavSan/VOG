#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <gtest/gtest.h>

#include "GraphicsApiFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests::Shader
{
namespace
{
const std::string kVertexShaderString = R"(
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


void main() {
    gl_Position = inPosition;
    fragColor = inColor;
}
)";

const std::string kFragmentShaderString = R"(
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
            mGraphicsProvider->getDevice(), ShadingStage::eVertex, kVertexShaderString);
        auto reflection = shader->reflect();

        EXPECT_EQ(reflection.uniformBuffers.size(), 1u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.set, 2u);
        EXPECT_EQ(reflection.uniformBuffers[0].location.binding, 3u);
    });
}

TEST_F(GraphicsProviderFixture, Shader_instantiate_fragment_shader)
{
    EXPECT_NO_THROW({
        auto shader = VOG::Graphics::Vulkan::Shader::create(
            mGraphicsProvider->getDevice(), ShadingStage::eFragment, kFragmentShaderString);
    });
}
} // namespace VOG::Tests::Shader
