#include "VOG/Graphics/Vulkan/Shader.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <glslang/Include/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <spirv_reflect.hpp>

#include <format>
#include <ranges>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{

constexpr TBuiltInResource kDefaultTBuiltInResource = {
    .maxLights                                 = 32,
    .maxClipPlanes                             = 6,
    .maxTextureUnits                           = 32,
    .maxTextureCoords                          = 32,
    .maxVertexAttribs                          = 64,
    .maxVertexUniformComponents                = 4096,
    .maxVaryingFloats                          = 64,
    .maxVertexTextureImageUnits                = 32,
    .maxCombinedTextureImageUnits              = 80,
    .maxTextureImageUnits                      = 32,
    .maxFragmentUniformComponents              = 4096,
    .maxDrawBuffers                            = 32,
    .maxVertexUniformVectors                   = 128,
    .maxVaryingVectors                         = 8,
    .maxFragmentUniformVectors                 = 16,
    .maxVertexOutputVectors                    = 16,
    .maxFragmentInputVectors                   = 15,
    .minProgramTexelOffset                     = -8,
    .maxProgramTexelOffset                     = 7,
    .maxClipDistances                          = 8,
    .maxComputeWorkGroupCountX                 = 65535,
    .maxComputeWorkGroupCountY                 = 65535,
    .maxComputeWorkGroupCountZ                 = 65535,
    .maxComputeWorkGroupSizeX                  = 1024,
    .maxComputeWorkGroupSizeY                  = 1024,
    .maxComputeWorkGroupSizeZ                  = 64,
    .maxComputeUniformComponents               = 1024,
    .maxComputeTextureImageUnits               = 16,
    .maxComputeImageUniforms                   = 8,
    .maxComputeAtomicCounters                  = 8,
    .maxComputeAtomicCounterBuffers            = 1,
    .maxVaryingComponents                      = 60,
    .maxVertexOutputComponents                 = 64,
    .maxGeometryInputComponents                = 64,
    .maxGeometryOutputComponents               = 128,
    .maxFragmentInputComponents                = 128,
    .maxImageUnits                             = 8,
    .maxCombinedImageUnitsAndFragmentOutputs   = 8,
    .maxCombinedShaderOutputResources          = 8,
    .maxImageSamples                           = 0,
    .maxVertexImageUniforms                    = 0,
    .maxTessControlImageUniforms               = 0,
    .maxTessEvaluationImageUniforms            = 0,
    .maxGeometryImageUniforms                  = 0,
    .maxFragmentImageUniforms                  = 8,
    .maxCombinedImageUniforms                  = 8,
    .maxGeometryTextureImageUnits              = 16,
    .maxGeometryOutputVertices                 = 256,
    .maxGeometryTotalOutputComponents          = 1024,
    .maxGeometryUniformComponents              = 1024,
    .maxGeometryVaryingComponents              = 64,
    .maxTessControlInputComponents             = 128,
    .maxTessControlOutputComponents            = 128,
    .maxTessControlTextureImageUnits           = 16,
    .maxTessControlUniformComponents           = 1024,
    .maxTessControlTotalOutputComponents       = 4096,
    .maxTessEvaluationInputComponents          = 128,
    .maxTessEvaluationOutputComponents         = 128,
    .maxTessEvaluationTextureImageUnits        = 16,
    .maxTessEvaluationUniformComponents        = 1024,
    .maxTessPatchComponents                    = 120,
    .maxPatchVertices                          = 32,
    .maxTessGenLevel                           = 64,
    .maxViewports                              = 16,
    .maxVertexAtomicCounters                   = 0,
    .maxTessControlAtomicCounters              = 0,
    .maxTessEvaluationAtomicCounters           = 0,
    .maxGeometryAtomicCounters                 = 0,
    .maxFragmentAtomicCounters                 = 8,
    .maxCombinedAtomicCounters                 = 8,
    .maxAtomicCounterBindings                  = 1,
    .maxVertexAtomicCounterBuffers             = 0,
    .maxTessControlAtomicCounterBuffers        = 0,
    .maxTessEvaluationAtomicCounterBuffers     = 0,
    .maxGeometryAtomicCounterBuffers           = 0,
    .maxFragmentAtomicCounterBuffers           = 1,
    .maxCombinedAtomicCounterBuffers           = 1,
    .maxAtomicCounterBufferSize                = 16384,
    .maxTransformFeedbackBuffers               = 4,
    .maxTransformFeedbackInterleavedComponents = 64,
    .maxCullDistances                          = 8,
    .maxCombinedClipAndCullDistances           = 8,
    .maxSamples                                = 4,
    .maxMeshOutputVerticesNV                   = 256,
    .maxMeshOutputPrimitivesNV                 = 512,
    .maxMeshWorkGroupSizeX_NV                  = 32,
    .maxMeshWorkGroupSizeY_NV                  = 1,
    .maxMeshWorkGroupSizeZ_NV                  = 1,
    .maxTaskWorkGroupSizeX_NV                  = 32,
    .maxTaskWorkGroupSizeY_NV                  = 1,
    .maxTaskWorkGroupSizeZ_NV                  = 1,
    .maxMeshViewCountNV                        = 4,
    .maxMeshOutputVerticesEXT                  = 256,
    .maxMeshOutputPrimitivesEXT                = 256,
    .maxMeshWorkGroupSizeX_EXT                 = 128,
    .maxMeshWorkGroupSizeY_EXT                 = 128,
    .maxMeshWorkGroupSizeZ_EXT                 = 128,
    .maxTaskWorkGroupSizeX_EXT                 = 128,
    .maxTaskWorkGroupSizeY_EXT                 = 128,
    .maxTaskWorkGroupSizeZ_EXT                 = 128,
    .maxMeshViewCountEXT                       = 4,
    .maxDualSourceDrawBuffersEXT               = 1,

    .limits = {
        .nonInductiveForLoops                 = true,
        .whileLoops                           = true,
        .doWhileLoops                         = true,
        .generalUniformIndexing               = true,
        .generalAttributeMatrixVectorIndexing = true,
        .generalVaryingIndexing               = true,
        .generalSamplerIndexing               = true,
        .generalVariableIndexing              = true,
        .generalConstantMatrixVectorIndexing  = true,
    }};

static EShLanguage
convertShadingStage(const vk::ShaderStageFlagBits shader_type)
{
    switch (shader_type)
    {
    case vk::ShaderStageFlagBits::eVertex:
        return EShLangVertex;
    case vk::ShaderStageFlagBits::eTessellationControl:
        return EShLangTessControl;
    case vk::ShaderStageFlagBits::eTessellationEvaluation:
        return EShLangTessEvaluation;
    case vk::ShaderStageFlagBits::eGeometry:
        return EShLangGeometry;
    case vk::ShaderStageFlagBits::eFragment:
        return EShLangFragment;
    case vk::ShaderStageFlagBits::eCompute:
        return EShLangCompute;
    default:
        return EShLangVertex;
    }
}

EShLanguage
convertShadingStage(Shader::ShadingStage stage)
{
    switch (stage)
    {
    case Shader::ShadingStage::eVertex:
        return EShLanguage::EShLangVertex;
    case Shader::ShadingStage::eFragment:
        return EShLanguage::EShLangFragment;
    default:
        return EShLangVertex;
    }
}

vk::ShaderStageFlagBits
toVulkan(Shader::ShadingStage stage)
{
    switch (stage)
    {
    case Shader::ShadingStage::eVertex:
        return vk::ShaderStageFlagBits::eVertex;
    case Shader::ShadingStage::eFragment:
        return vk::ShaderStageFlagBits::eFragment;
    default:
        throw std::invalid_argument{"Unhandled ShadingStage value in toVulkan()"};
    }
}

std::vector<std::uint32_t>
compileGLSLShader(Shader::ShadingStage stage, const std::string& glslCode)
{
    const auto shadingStage   = convertShadingStage(stage);
    const auto messagesFilter = EShMessages::EShMsgDefault;

    glslang::TShader shader{shadingStage};
    shader.setDebugInfo(true);

    const char* str    = glslCode.c_str();
    const int   length = static_cast<int>(glslCode.size());
    shader.setStringsWithLengths(&str, &length, 1);
    shader.setEntryPoint("main");
    shader.setSourceEntryPoint("main");

    shader.setEnvInput(glslang::EShSource::EShSourceGlsl,
                       shadingStage,
                       glslang::EShClient::EShClientVulkan,
                       glslang::EShTargetClientVersion::EShTargetVulkan_1_2);

    shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                        glslang::EShTargetClientVersion::EShTargetVulkan_1_2);

    shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                        glslang::EShTargetLanguageVersion::EShTargetSpv_1_5);

    auto includer = glslang::TShader::ForbidIncluder{};
    if (!shader.parse(&kDefaultTBuiltInResource,
                      glslang::EShTargetClientVersion::EShTargetOpenGL_450,
                      false,
                      messagesFilter,
                      includer))
    {
        throw Shader::CompilationError{
            std::format("Shader info log:\n{}"
                        "Debug log:\n{}",
                        shader.getInfoLog(),
                        shader.getInfoDebugLog())};
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messagesFilter))
    {
        throw Shader::CompilationError{std::format("Shader info log:\n{}", program.getInfoLog())};
    }

    std::vector<std::uint32_t> spirv{};
    glslang::GlslangToSpv(*program.getIntermediate(shadingStage), spirv);

    return spirv;
}

Shader::Reflection::BaseType
attributeSpirVTypeToVulkanType(const spirv_cross::SPIRType& spirvType)
{
    Shader::Reflection::BaseType::Type type;
    switch (spirvType.basetype)
    {
    case spirv_cross::SPIRType::BaseType::Int:
        type = Shader::Reflection::BaseType::Type::eInt;
        break;
    case spirv_cross::SPIRType::BaseType::UInt:
        type = Shader::Reflection::BaseType::Type::eUInt;
        break;
    case spirv_cross::SPIRType::BaseType::Float:
        type = Shader::Reflection::BaseType::Type::eFloat;
        break;
    case spirv_cross::SPIRType::BaseType::Double:
        type = Shader::Reflection::BaseType::Type::eDouble;
        break;
    default:
        throw std::runtime_error{"spirv_cross::SPIRType is not handled."};
    }

    return {
        .type       = type,
        .components = static_cast<std::uint8_t>(spirvType.vecsize),
        .columns    = static_cast<std::uint8_t>(spirvType.columns),
    };
}

Shader::Reflection::StageAttributes
processStageResources(const spirv_cross::Compiler&   compiler,
                      const std::ranges::range auto& resources)
    requires std::same_as<std::ranges::range_value_t<decltype(resources)>, spirv_cross::Resource>
{
    Shader::Reflection::StageAttributes result{};
    for (const spirv_cross::Resource& resource : resources)
    {
        const std::uint32_t location =
            compiler.get_decoration(resource.id, spv::Decoration::DecorationLocation);

        const auto& resourceType = compiler.get_type(resource.base_type_id);
        result.push_back({.location = static_cast<std::uint8_t>(location),
                          .name     = resource.name,
                          .format   = attributeSpirVTypeToVulkanType(resourceType)});
    }

    std::ranges::sort(result,
                      [](const auto& left, const auto& right)
                      {
                          return left.location < right.location;
                      });

    return result;
}

std::vector<Shader::Reflection::StructMember>
parseStructMembers(const spirv_cross::Compiler& compiler, const spirv_cross::Resource& resource)
{
    const auto  baseTypeId = resource.base_type_id;
    const auto& structType = compiler.get_type(baseTypeId);
    VOG_ASSERT_MSG(structType.basetype == spirv_cross::SPIRType::BaseType::Struct,
                   "Push constants block must be a struct.");

    std::vector<Shader::Reflection::StructMember> members;
    for (std::uint32_t idx = 0; idx < structType.member_types.size(); ++idx)
    {
        const auto& member     = structType.member_types[idx];
        const auto& memberType = compiler.get_type(member);

        const auto& name   = compiler.get_member_name(baseTypeId, idx);
        const auto  offset = compiler.type_struct_member_offset(structType, idx);
        const auto  size   = compiler.get_declared_struct_member_size(structType, idx);

        VOG_ASSERT_MSG(
            offset < std::numeric_limits<decltype(Shader::Reflection::StructMember::offset)>::max(),
            "Should fit into offset.");
        VOG_ASSERT_MSG(
            size < std::numeric_limits<decltype(Shader::Reflection::StructMember::size)>::max(),
            "Should fit into size.");

        members.push_back(
            {.offset = static_cast<decltype(Shader::Reflection::StructMember::offset)>(offset),
             .size   = static_cast<decltype(Shader::Reflection::StructMember::size)>(size),
             .name   = name,
             .type   = attributeSpirVTypeToVulkanType(memberType)});
    }

    std::sort(members.begin(),
              members.end(),
              [](const auto& left, const auto& right)
              {
                  return left.offset < right.offset;
              });

    return members;
}

void
checkResourceLocation(const Shader::Reflection::ResourceLocation& location)
{
    if (location.set > Limits::gMaxNumDescriptorSets)
    {
        throw Shader::ShaderError{std::format("Resource set \"{}\" exceeds the limit of \"{}\"",
                                              location.set,
                                              Limits::gMaxNumDescriptorSets)};
    }
    if (location.binding > Limits::gMaxNumDescriptorBindings)
    {
        throw Shader::ShaderError{std::format("Resource binding \"{}\" exceeds the limit of \"{}\"",
                                              location.binding,
                                              Limits::gMaxNumDescriptorBindings)};
    }
}

Shader::Reflection
reflect(const std::vector<std::uint32_t>& binary)
{
    spirv_cross::Compiler              compiler{binary};
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    Shader::Reflection reflection{};

    VOG_ASSERT(resources.stage_inputs.size() < Limits::gMaxNumStageAttributes);
    VOG_ASSERT(resources.stage_outputs.size() < Limits::gMaxNumStageAttributes);
    reflection.inAttributes  = processStageResources(compiler, resources.stage_inputs);
    reflection.outAttributes = processStageResources(compiler, resources.stage_outputs);

    VOG_ASSERT_MSG(resources.push_constant_buffers.size() <= 1u,
                   "Can only be one push constant block.");
    if (!resources.push_constant_buffers.empty())
    {
        const auto& pushConstantsBlock     = resources.push_constant_buffers[0];
        reflection.pushConstants.variables = parseStructMembers(compiler, pushConstantsBlock);
    }

    for (const spirv_cross::Resource& resource : resources.uniform_buffers)
    {
        const Shader::Reflection::ResourceLocation location = {
            .set = compiler.get_decoration(resource.id, spv::Decoration::DecorationDescriptorSet),
            .binding = compiler.get_decoration(resource.id, spv::Decoration::DecorationBinding)};
        checkResourceLocation(location);

        const auto& baseType = compiler.get_type(resource.base_type_id);
        const auto  size     = compiler.get_declared_struct_size(baseType);

        reflection.uniformBuffers.push_back({.location = location,
                                             .size     = size,
                                             .members  = parseStructMembers(compiler, resource)});
    }
    std::sort(reflection.uniformBuffers.begin(),
              reflection.uniformBuffers.end(),
              [](const auto& left, const auto& right)
              {
                  return left.location < right.location;
              });

    return reflection;
}
} // namespace

std::shared_ptr<Shader>
Shader::create(DevicePtr device, Shader::ShadingStage stage, const std::string& glslCode)
{
    return std::make_shared<Shader>(std::move(device), stage, glslCode);
}

bool
Shader::vertexFormatCompatible(Shader::Reflection::BaseType shaderVertexFormat, vk::Format provided)
{
    // Todo: implement compatibility checking.
    return true;
}

Shader::Shader(DevicePtr device, Shader::ShadingStage stage, const std::string& glslCode)
    : Shader(std::move(device), stage, compileGLSLShader(stage, glslCode))
{
}

Shader::Shader(DevicePtr                  _device,
               Shader::ShadingStage       stage,
               std::vector<std::uint32_t> shaderBinary)
    : device{std::move(_device)}
    , stage{toVulkan(stage)}
    , binary{std::move(shaderBinary)}
    , module{*device,
             vk::ShaderModuleCreateInfo{.codeSize = binary.size() * sizeof(std::uint32_t),
                                        .pCode    = binary.data()}}
    , reflection{reflect(binary)}
{
}
} // namespace VOG::Graphics::Vulkan
