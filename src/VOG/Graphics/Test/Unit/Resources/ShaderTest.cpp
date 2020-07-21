#include <VOG/Graphics/Resources/Shader.hpp>
#include <gtest/gtest.h>

#include "Api/GraphicsApiFixture.hpp"

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
    std::shared_ptr<VOG::Graphics::Resources::Shader> shader;
    EXPECT_NO_THROW(shader = std::make_shared<VOG::Graphics::Resources::Shader>(
                        m_graphicsProvider, "default",
                        VOG::Graphics::Resources::Shader::ShaderStage::Vertex,
                        kVertexShaderString));
}

TEST_F(GraphicsProviderFixture, Shader_instantiate_fragment_shader)
{
    std::shared_ptr<VOG::Graphics::Resources::Shader> shader;
    EXPECT_NO_THROW(shader = std::make_shared<VOG::Graphics::Resources::Shader>(
                        m_graphicsProvider, "default",
                        VOG::Graphics::Resources::Shader::ShaderStage::Fragment,
                        kFragmentShaderString));
}
} // namespace VOG::Tests::Shader