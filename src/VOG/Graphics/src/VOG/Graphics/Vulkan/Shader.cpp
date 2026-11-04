#include "VOG/Graphics/Vulkan/Shader.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spirv_reflect.hpp>
#include <vulkan/vulkan_format_traits.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

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

/** Bits of a double, and of a 64-bit integer. */
constexpr std::uint32_t gWideBits = 64u;

/**
 * Consecutive locations a value of @p type takes: the array sizes multiplied, times one location
 * per matrix column, times two for a 64-bit vector of three or four components.
 */
std::uint32_t
locationCountOf(const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
{
    constexpr std::uint32_t kTwoLocationMin = 3u;

    std::uint32_t count = std::max<std::uint32_t>(type.columns, 1u);
    if (type.width == gWideBits && type.vecsize >= kTwoLocationMin)
    {
        count *= 2u;
    }
    // A dimension that is not a literal holds the ID of a constant (a specialization constant),
    // whose default is the size.
    for (const auto [dimension, isLiteral] : std::views::zip(type.array, type.array_size_literal))
    {
        count *= isLiteral ? dimension : compiler.evaluate_constant_u32(dimension);
    }

    return count;
}

/**
 * One attribute per member of the struct that @p variable is: an interface block or a struct
 * variable. The name of an attribute is "Name.member", where Name is the name of the block, or of
 * the variable if it is no block. A member takes the location of its own Location decoration,
 * else the location after the member before it, and the first one the location of the variable.
 */
Shader::Reflection::StageAttributes
processStructMembers(const spirv_cross::Compiler& compiler, const spirv_cross::Resource& variable)
{
    const spirv_cross::SPIRType& structType = compiler.get_type(variable.base_type_id);
    if (!compiler.get_type(variable.type_id).array.empty())
    {
        throw Shader::ShaderError{
            std::format("The stage variable \"{}\" is an array of structs; that is not supported.",
                        variable.name)};
    }

    Shader::Reflection::StageAttributes result;
    std::uint32_t next = compiler.get_decoration(variable.id, spv::Decoration::DecorationLocation);
    for (const auto [index, memberTypeId] :
         std::views::zip(std::views::iota(0u), structType.member_types))
    {
        const std::string& memberName = compiler.get_member_name(variable.base_type_id, index);
        const auto&        memberType = compiler.get_type(memberTypeId);
        if (memberType.basetype == spirv_cross::SPIRType::BaseType::Struct)
        {
            throw Shader::ShaderError{
                std::format("The stage variable \"{}\" has the struct \"{}\" as a member; a "
                            "member must be a vector, a matrix or a scalar (or an array of one).",
                            variable.name,
                            memberName)};
        }

        if (compiler.has_member_decoration(
                variable.base_type_id, index, spv::Decoration::DecorationLocation))
        {
            next = compiler.get_member_decoration(
                variable.base_type_id, index, spv::Decoration::DecorationLocation);
        }
        const std::uint32_t locationCount = locationCountOf(compiler, memberType);

        result.push_back({.location      = static_cast<std::uint8_t>(next),
                          .name          = std::format("{}.{}", variable.name, memberName),
                          .format        = attributeSpirVTypeToVulkanType(memberType),
                          .locationCount = locationCount});
        next += locationCount;
    }

    return result;
}

/** A struct (an interface block, or a struct variable) yields one attribute per member, any other
 *  variable one. */
Shader::Reflection::StageAttributes
processStageResources(const spirv_cross::Compiler&   compiler,
                      const std::ranges::range auto& resources)
    requires std::same_as<std::ranges::range_value_t<decltype(resources)>, spirv_cross::Resource>
{
    Shader::Reflection::StageAttributes result{};
    for (const spirv_cross::Resource& resource : resources)
    {
        const auto& resourceType = compiler.get_type(resource.base_type_id);
        if (resourceType.basetype == spirv_cross::SPIRType::BaseType::Struct)
        {
            std::ranges::move(processStructMembers(compiler, resource), std::back_inserter(result));
            continue;
        }

        const std::uint32_t location =
            compiler.get_decoration(resource.id, spv::Decoration::DecorationLocation);

        // The base type has no array dimensions; the variable's own type does.
        result.push_back(
            {.location      = static_cast<std::uint8_t>(location),
             .name          = resource.name,
             .format        = attributeSpirVTypeToVulkanType(resourceType),
             .locationCount = locationCountOf(compiler, compiler.get_type(resource.type_id))});
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

/**
 * Pipelines have no layout, so a resource declared with a set and a binding could never be bound.
 * SPIRV-Cross lists none of the heap resources here (ShaderTest, Shader_heapShaderReflects...).
 */
void
rejectDescriptorSetResources(const spirv_cross::ShaderResources& resources)
{
    const std::pair<const char*, const spirv_cross::SmallVector<spirv_cross::Resource>&> kinds[] = {
        {"uniform buffer", resources.uniform_buffers},
        {"storage buffer", resources.storage_buffers},
        {"sampled image", resources.sampled_images},
        {"separate image", resources.separate_images},
        {"separate sampler", resources.separate_samplers},
        {"storage image", resources.storage_images},
        {"acceleration structure", resources.acceleration_structures},
        {"subpass input", resources.subpass_inputs},
    };
    for (const auto& [kind, list] : kinds)
    {
        if (!list.empty())
        {
            throw Shader::ShaderError{
                std::format("The shader declares the {} \"{}\" with a set and a binding. Shaders "
                            "reach their resources through the descriptor heap "
                            "(layout(descriptor_heap)); pipelines have no layout.",
                            kind,
                            list.front().name)};
        }
    }
}

/** A pipeline takes at most Limits::gMaxNumVertexInputs vertex inputs. */
void
rejectTooManyVertexInputs(std::size_t count)
{
    if (count > Limits::gMaxNumVertexInputs)
    {
        throw Shader::ShaderError{
            std::format("The shader declares {} vertex inputs; at most {} are supported.",
                        count,
                        Limits::gMaxNumVertexInputs)};
    }
}

Shader::Reflection
reflect(std::span<const std::uint32_t> binary, Shader::ShadingStage stage)
{
    const spirv_cross::Compiler        compiler{binary.data(), binary.size()};
    const spirv_cross::ShaderResources resources = compiler.get_shader_resources();
    rejectDescriptorSetResources(resources);

    Shader::Reflection reflection{};

    reflection.inAttributes  = processStageResources(compiler, resources.stage_inputs);
    reflection.outAttributes = processStageResources(compiler, resources.stage_outputs);
    if (stage == Shader::ShadingStage::eVertex)
    {
        rejectTooManyVertexInputs(reflection.inAttributes.size());
    }

    VOG_ASSERT_MSG(resources.push_constant_buffers.size() <= 1u,
                   "Can only be one push constant block.");
    if (!resources.push_constant_buffers.empty())
    {
        const auto& pushConstantsBlock     = resources.push_constant_buffers[0];
        reflection.pushConstants.variables = parseStructMembers(compiler, pushConstantsBlock);
    }

    return reflection;
}
} // namespace

bool
Shader::vertexFormatCompatible(Shader::Reflection::BaseType shaderVertexFormat, vk::Format provided)
{
    using Type = Shader::Reflection::BaseType::Type;

    if (shaderVertexFormat.type == Type::eDouble || provided == vk::Format::eUndefined ||
        vk::componentBits(provided, 0u) == gWideBits)
    {
        return false;
    }

    // The same split as the numeric type of a format: only SINT and UINT are integers.
    const std::string_view numeric = vk::componentNumericFormat(provided, 0u);
    switch (shaderVertexFormat.type)
    {
    case Type::eInt:
        return numeric == "SINT";
    case Type::eUInt:
        return numeric == "UINT";
    default:
        return numeric != "SINT" && numeric != "UINT";
    }
}

Shader::Shader(DevicePtr _device, Shader::ShadingStage stage, std::span<const std::uint32_t> binary)
    : device{std::move(_device)}
    , stage{toVulkan(stage)}
    , module{device->raii(),
             vk::ShaderModuleCreateInfo{.codeSize = binary.size() * sizeof(std::uint32_t),
                                        .pCode    = binary.data()}}
    , reflection{reflect(binary, stage)}
{
}
} // namespace VOG::Graphics::Vulkan
