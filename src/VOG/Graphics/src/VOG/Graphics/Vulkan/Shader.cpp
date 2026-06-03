#include "VOG/Graphics/Vulkan/Shader.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spirv_reflect.hpp>

#include <format>
#include <ranges>

namespace VOG::Graphics::Vulkan
{
namespace
{

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

    return Shader::Reflection::BaseType{
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
reflect(std::span<const std::uint32_t> binary)
{
    const spirv_cross::Compiler        compiler{binary.data(), binary.size()};
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

bool
Shader::vertexFormatCompatible(Shader::Reflection::BaseType shaderVertexFormat, vk::Format provided)
{
    // Todo: implement compatibility checking.
    return true;
}

Shader::Shader(DevicePtr _device, Shader::ShadingStage stage, std::span<const std::uint32_t> binary)
    : device{std::move(_device)}
    , stage{toVulkan(stage)}
    , module{*device,
             vk::ShaderModuleCreateInfo{.codeSize = binary.size() * sizeof(std::uint32_t),
                                        .pCode    = binary.data()}}
    , reflection{reflect(binary)}
{
}
} // namespace VOG::Graphics::Vulkan
