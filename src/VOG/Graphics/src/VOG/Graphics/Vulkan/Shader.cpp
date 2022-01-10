#include "VOG/Graphics/Vulkan/Shader.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <shaderc/shaderc.hpp>

#include <spirv_reflect.hpp>

namespace VOG::Graphics::Vulkan
{
namespace
{
shaderc_shader_kind
ConvertShadingStage(ShadingStage stage)
{
    switch (stage)
    {
    case ShadingStage::eVertex:
        return shaderc_vertex_shader;
    case ShadingStage::eFragment:
        return shaderc_fragment_shader;
    default:
        return shaderc_glsl_infer_from_source;
    }

    return shaderc_glsl_infer_from_source;
}

std::vector<std::uint32_t>
compileGLSLShader(ShadingStage stage, const std::string& glslCode)
{
    shaderc::Compiler       compiler;
    shaderc::CompileOptions options;

    const shaderc::SpvCompilationResult resultCompile =
        compiler.CompileGlslToSpv(glslCode, ConvertShadingStage(stage), "main", options);
    if (resultCompile.GetCompilationStatus() != shaderc_compilation_status_success)
    {
        throw std::runtime_error{"Failed to compile shader with error:\n" +
                                 resultCompile.GetErrorMessage()};
    }

    return {resultCompile.begin(), resultCompile.end()};
}
} // namespace

std::shared_ptr<Shader>
Shader::create(const Device& device, ShadingStage stage, const std::string& source)
{
    return std::make_shared<Shader>(device, stage, source);
}

Shader::Shader(const Device& device, ShadingStage stage, const std::string& glslCode)
    : Shader(device, stage, compileGLSLShader(stage, glslCode))
{
}

Shader::Shader(const Device& device, ShadingStage stage, std::vector<std::uint32_t> shaderBinary)
    : stage{stage}
    , binary{std::move(shaderBinary)}
    , module{device,
             vk::ShaderModuleCreateInfo{.codeSize = binary.size() * sizeof(std::uint32_t),
                                        .pCode    = binary.data()}}
{
}

Shader::Reflection
Shader::reflect() const
{
    spirv_cross::Compiler              spirvCompiller{binary};
    const spirv_cross::ShaderResources resources = spirvCompiller.get_shader_resources();

    Reflection reflection{};
    for (const spirv_cross::Resource& resource : resources.uniform_buffers)
    {
        const std::uint32_t set =
            spirvCompiller.get_decoration(resource.id, spv::Decoration::DecorationDescriptorSet);
        const std::uint32_t binding =
            spirvCompiller.get_decoration(resource.id, spv::Decoration::DecorationBinding);

        const auto& baseType = spirvCompiller.get_type(resource.base_type_id);
        const auto  size     = spirvCompiller.get_declared_struct_size(baseType);

        Reflection::UniformBuffer buffer{.location = {.set = set, .binding = binding},
                                         .size     = size};

        reflection.uniformBuffers.push_back(buffer);
    }

    return reflection;
}
} // namespace VOG::Graphics::Vulkan
