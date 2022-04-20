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
        throw Shader::CompilationError{resultCompile.GetErrorMessage()};
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
    spirv_cross::Compiler              compiler{binary};
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    Reflection reflection{};

    VOG_ASSERT_MSG(resources.push_constant_buffers.size() <= 1u,
                   "Can only be one push constant block.");
    if (resources.push_constant_buffers.size() == 1u)
    {
        const auto  pushConstantStructId = resources.push_constant_buffers[0].base_type_id;
        const auto& pushConstantStruct   = compiler.get_type(pushConstantStructId);
        VOG_ASSERT_MSG(pushConstantStruct.basetype == spirv_cross::SPIRType::BaseType::Struct,
                       "Push constants block must be a struct.");

        reflection.pushConstants.size = compiler.get_declared_struct_size(pushConstantStruct);
        for (std::uint32_t idx = 0; idx < pushConstantStruct.member_types.size(); ++idx)
        {
            const auto& member     = pushConstantStruct.member_types[idx];
            const auto& memberType = compiler.get_type(member);

            const auto& name   = compiler.get_member_name(pushConstantStructId, idx);
            const auto  offset = compiler.type_struct_member_offset(pushConstantStruct, idx);
            const auto  size   = compiler.get_declared_struct_member_size(pushConstantStruct, idx);

            // Validate offset and size as push constants should be no more than 256 bytes size.
            VOG_ASSERT_MSG(offset < std::numeric_limits<std::uint8_t>::max(),
                           "Should fit into 8 bit unsigned.");
            VOG_ASSERT_MSG(size < std::numeric_limits<std::uint8_t>::max(),
                           "Should fit into 8 bit unsigned.");

            reflection.pushConstants.variables.push_back(
                {.name   = name,
                 .offset = static_cast<std::uint8_t>(offset),
                 .size   = static_cast<std::uint8_t>(size)});
        }
    }

    for (const spirv_cross::Resource& resource : resources.uniform_buffers)
    {
        const std::uint32_t set =
            compiler.get_decoration(resource.id, spv::Decoration::DecorationDescriptorSet);
        const std::uint32_t binding =
            compiler.get_decoration(resource.id, spv::Decoration::DecorationBinding);

        const auto& baseType = compiler.get_type(resource.base_type_id);
        const auto  size     = compiler.get_declared_struct_size(baseType);

        Reflection::UniformBuffer buffer{.location = {.set = set, .binding = binding},
                                         .size     = size};

        reflection.uniformBuffers.push_back(buffer);
    }

    return reflection;
}
} // namespace VOG::Graphics::Vulkan
