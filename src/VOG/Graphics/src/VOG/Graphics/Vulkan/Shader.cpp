#include "VOG/Graphics/Vulkan/Shader.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <spirv_reflect.hpp>
#include <vulkan/vulkan_format_traits.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

using BlockMember = Shader::Reflection::BlockMember;
using HeapBlock   = Shader::Reflection::HeapBlock;

/** The ShaderError for a member of @p blockName that is of a type the reflection has no name for.
 */
Shader::ShaderError
unreflectableMember(std::string_view blockName, std::string_view memberName)
{
    return Shader::ShaderError{
        std::format("The block \"{}\" has the member \"{}\" of a type VOG cannot reflect; "
                    "members are 32-bit int, uint and float scalars, vectors and matrices, "
                    "double, or structs of those.",
                    blockName,
                    memberName)};
}

/** The numeric type of a block member, or a ShaderError that names the member. */
Shader::Reflection::BaseType
blockMemberBaseType(const spirv_cross::SPIRType& type,
                    std::string_view             blockName,
                    std::string_view             memberName)
{
    switch (type.basetype)
    {
    case spirv_cross::SPIRType::BaseType::Int:
    case spirv_cross::SPIRType::BaseType::UInt:
    case spirv_cross::SPIRType::BaseType::Float:
    case spirv_cross::SPIRType::BaseType::Double:
        return attributeSpirVTypeToVulkanType(type);
    default:
        throw unreflectableMember(blockName, memberName);
    }
}

/** The elements of an array of @p type, all dimensions multiplied; 0 without an array. */
std::uint32_t
arraySizeOf(const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
{
    if (type.array.empty())
    {
        return 0u;
    }

    std::uint32_t count = 1u;
    for (const auto [dimension, isLiteral] : std::views::zip(type.array, type.array_size_literal))
    {
        count *= isLiteral ? dimension : compiler.evaluate_constant_u32(dimension);
    }

    return count;
}

/**
 * The members of @p structType, in declaration order, with the layout the shader gave them. Every
 * member is read through the member type of its parent: a struct that SPIR-V copied for an access
 * has no Offset decorations, only the type the block names has them.
 */
std::vector<BlockMember>
readMembers(const spirv_cross::Compiler& compiler,
            const spirv_cross::SPIRType& structType,
            std::string_view             blockName)
{
    std::vector<BlockMember> members;
    for (const auto [index, memberTypeId] :
         std::views::zip(std::views::iota(0u), structType.member_types))
    {
        const spirv_cross::SPIRType& memberType = compiler.get_type(memberTypeId);

        BlockMember member{
            .name   = compiler.get_member_name(structType.self, index),
            .offset = compiler.type_struct_member_offset(structType, index),
            .size   = static_cast<std::uint32_t>(
                compiler.get_declared_struct_member_size(structType, index)),
            .arraySize = arraySizeOf(compiler, memberType),
            .rowMajor  = compiler.has_member_decoration(
                structType.self, index, spv::Decoration::DecorationRowMajor),
        };
        if (!memberType.array.empty() &&
            compiler.has_decoration(memberTypeId, spv::Decoration::DecorationArrayStride))
        {
            member.arrayStride =
                compiler.get_decoration(memberTypeId, spv::Decoration::DecorationArrayStride);
        }

        // A pointer to a struct (a buffer_reference) has the members of that struct, which can
        // hold a pointer to the struct again: following it would not end.
        if (memberType.pointer)
        {
            throw unreflectableMember(blockName, member.name);
        }

        if (memberType.basetype == spirv_cross::SPIRType::BaseType::Struct)
        {
            member.structName = compiler.get_name(memberType.self);
            member.members    = readMembers(compiler, memberType, blockName);
        }
        else
        {
            member.type = blockMemberBaseType(memberType, blockName, member.name);
            if (memberType.columns > 1u)
            {
                member.matrixStride = compiler.type_struct_member_matrix_stride(structType, index);
            }
        }

        members.push_back(std::move(member));
    }

    return members;
}

std::vector<BlockMember>
parsePushConstants(const spirv_cross::Compiler& compiler, const spirv_cross::Resource& resource)
{
    const spirv_cross::SPIRType& structType = compiler.get_type(resource.base_type_id);
    VOG_ASSERT_MSG(structType.basetype == spirv_cross::SPIRType::BaseType::Struct,
                   "Push constants block must be a struct.");

    std::vector<BlockMember> members =
        readMembers(compiler, structType, compiler.get_name(structType.self));
    std::ranges::stable_sort(members, std::ranges::less{}, &BlockMember::offset);

    return members;
}

/** Words of the SPIR-V header: magic, version, generator, id bound, schema. */
constexpr std::size_t gSpirvHeaderWords = 5u;

/** An instruction starts with the opcode in its low 16 bits and its word count in the high ones. */
constexpr std::uint32_t gOpcodeMask     = 0xFFFFu;
constexpr std::uint32_t gWordCountShift = 16u;

/**
 * The `descriptor_stride` of the heap array of the type @p arrayTypeId, or 0 when the array has
 * none or its stride is not a constant (it is then the size of a descriptor).
 */
std::uint32_t
heapArrayStride(const spirv_cross::Compiler& compiler, std::uint32_t arrayTypeId)
{
    if (!compiler.has_decoration(arrayTypeId, spv::Decoration::DecorationArrayStrideIdEXT))
    {
        return 0u;
    }

    const std::uint32_t strideId =
        compiler.get_decoration(arrayTypeId, spv::Decoration::DecorationArrayStrideIdEXT);
    if (compiler.get_ir().ids[strideId].get_type() != spirv_cross::TypeConstant)
    {
        return 0u;
    }

    const spirv_cross::SPIRConstant& constant = compiler.get_constant(strideId);

    return constant.size_of_type == 0u ? constant.scalar() : 0u;
}

/** Whether @p variableId is the variable the resource heap is read from. */
bool
isResourceHeap(const spirv_cross::Compiler& compiler, std::uint32_t variableId)
{
    return compiler.has_decoration(variableId, spv::Decoration::DecorationBuiltIn) &&
           compiler.get_decoration(variableId, spv::Decoration::DecorationBuiltIn) ==
               spv::BuiltIn::BuiltInResourceHeapEXT;
}

/** Whether @p typeId is a struct decorated Block: what a heap block is in the SPIR-V. */
bool
isHeapBlockType(const spirv_cross::Compiler& compiler, std::uint32_t typeId)
{
    // The base type of a step into the heap can be a type SPIRV-Cross keeps no SPIRType for.
    if (compiler.get_ir().ids[typeId].get_type() != spirv_cross::TypeType)
    {
        return false;
    }

    const spirv_cross::SPIRType& type = compiler.get_type(typeId);

    return type.basetype == spirv_cross::SPIRType::BaseType::Struct && !type.pointer &&
           compiler.has_decoration(type.self, spv::Decoration::DecorationBlock);
}

/**
 * Finds the blocks a stage reads through the descriptor heap.
 *
 * A heap block exists in the SPIR-V only as the base type of an untyped access chain that starts
 * at the pointer OpBufferPointerEXT makes from an access chain into the resource heap:
 *
 *   %slot = OpUntypedAccessChainKHR %ptr %heapArrayType %resource_heap %index
 *   %buf  = OpBufferPointerEXT      %ptr %slot
 *   %member = OpUntypedAccessChainKHR %ptr %Block %buf %memberIndex
 *
 * so the walk follows %member back to %slot to read the stride of the heap array. Every access
 * makes its own copy of %Block, so the copies are merged by name. A block that no instruction
 * reaches is not in the SPIR-V.
 *
 * This form is recognized, and the walk fails closed. Every access makes its own buffer pointer,
 * and an atomic on a member, or the length of an unsized array, makes one that never becomes the
 * base of such a chain. Such an access is one the walk cannot see, so finish() throws a
 * ShaderError for it, also when plain reads of the same block sit beside it.
 */
class HeapBlockWalker
{
public:
    explicit HeapBlockWalker(const spirv_cross::Compiler& compiler)
        : mCompiler{compiler}
    {
    }

    /** Looks at one instruction, @p instruction[0] being its opcode word. */
    void
    visit(std::span<const std::uint32_t> instruction)
    {
        switch (static_cast<spv::Op>(instruction[0] & gOpcodeMask))
        {
        case spv::Op::OpBufferPointerEXT:
            mUnclaimedPointers.insert(instruction[kResultId]);
            if (const auto source = mStrideOfPointer.find(instruction[kPointerSource]);
                source != mStrideOfPointer.end())
            {
                mStrideOfPointer[instruction[kResultId]] = source->second;
            }
            break;

        case spv::Op::OpUntypedAccessChainKHR:
        case spv::Op::OpUntypedInBoundsAccessChainKHR:
            visitAccessChain(instruction);
            break;

        default:
            break;
        }
    }

    /**
     * The blocks found, sorted by name.
     *
     * @throws Shader::ShaderError if a buffer pointer never became the base of a block access: an
     *         atomic on a heap block, or the length of an unsized array in one.
     */
    std::vector<HeapBlock>
    finish()
    {
        if (!mUnclaimedPointers.empty())
        {
            throw Shader::ShaderError{
                "The shader uses an atomic on a heap block, or asks for the length of an unsized "
                "array in one; VOG does not reflect a heap block used that way."};
        }

        std::vector<HeapBlock> result;
        result.reserve(mBlocks.size());
        std::ranges::move(mBlocks | std::views::values, std::back_inserter(result));

        return result;
    }

private:
    // Operand positions of the instructions, counted from the opcode word.
    static constexpr std::size_t kResultType    = 1u;
    static constexpr std::size_t kResultId      = 2u;
    static constexpr std::size_t kBaseType      = 3u;
    static constexpr std::size_t kBase          = 4u;
    static constexpr std::size_t kPointerSource = 3u;

    void
    visitAccessChain(std::span<const std::uint32_t> instruction)
    {
        const std::uint32_t baseTypeId = instruction[kBaseType];
        if (!isHeapBlockType(mCompiler, baseTypeId))
        {
            // Not a block: a step into the heap array itself, whose stride the buffer pointer
            // after it hands on to the block.
            if (isResourceHeap(mCompiler, instruction[kBase]))
            {
                mStrideOfPointer[instruction[kResultId]] = heapArrayStride(mCompiler, baseTypeId);
            }
            return;
        }

        mUnclaimedPointers.erase(instruction[kBase]);

        const spirv_cross::SPIRType& baseType = mCompiler.get_type(baseTypeId);
        const auto                   chained  = mStrideOfPointer.find(instruction[kBase]);
        const std::uint32_t stride = chained != mStrideOfPointer.end() ? chained->second : 0u;
        const std::string&  name   = mCompiler.get_name(baseType.self);

        if (mBlocks.contains(name))
        {
            // A copy of a block seen before: the same declaration, so the same members and stride.
            return;
        }

        const spv::StorageClass storage = mCompiler.get_type(instruction[kResultType]).storage;
        mBlocks.emplace(
            name,
            HeapBlock{
                .name    = name,
                .storage = storage == spv::StorageClass::StorageClassStorageBuffer
                             ? Shader::Reflection::HeapBlockStorage::eStorageBuffer
                             : Shader::Reflection::HeapBlockStorage::eUniform,
                .size    = static_cast<std::uint32_t>(mCompiler.get_declared_struct_size(baseType)),
                .strideDecoration = stride,
                .members          = readMembers(mCompiler, baseType, name),
            });
    }

    const spirv_cross::Compiler& mCompiler;

    /** The stride of the heap array behind a pointer: by the id of an access chain into the heap,
     *  and by the id of the buffer pointer made from it. */
    std::unordered_map<std::uint32_t, std::uint32_t> mStrideOfPointer;
    std::map<std::string, HeapBlock>                 mBlocks;

    /** Buffer pointers that no block access started from yet. */
    std::set<std::uint32_t> mUnclaimedPointers;
};

std::vector<HeapBlock>
reflectHeapBlocks(const spirv_cross::Compiler& compiler, std::span<const std::uint32_t> binary)
{
    HeapBlockWalker walker{compiler};
    for (std::size_t at = gSpirvHeaderWords; at < binary.size();)
    {
        const std::size_t wordCount = binary[at] >> gWordCountShift;
        VOG_ASSERT_MSG(wordCount != 0u && at + wordCount <= binary.size(), "Malformed SPIR-V.");
        walker.visit(binary.subspan(at, wordCount));
        at += wordCount;
    }

    return walker.finish();
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
        reflection.pushConstants = parsePushConstants(compiler, resources.push_constant_buffers[0]);
    }
    reflection.heapBlocks = reflectHeapBlocks(compiler, binary);

    return reflection;
}
} // namespace

std::string
Shader::describe(Reflection::BaseType type)
{
    using Type = Reflection::BaseType::Type;

    const char* prefix = "";
    const char* scalar = "float";
    switch (type.type)
    {
    case Type::eInt:
        prefix = "i";
        scalar = "int";
        break;
    case Type::eUInt:
        prefix = "u";
        scalar = "uint";
        break;
    case Type::eDouble:
        prefix = "d";
        scalar = "double";
        break;
    default:
        break;
    }

    if (type.columns > 1u)
    {
        // GLSL names a matrix by columns, then rows: mat2x3 has 2 columns of 3 components.
        return type.columns == type.components
                 ? std::format("{}mat{}", prefix, std::uint32_t{type.columns})
                 : std::format("{}mat{}x{}",
                               prefix,
                               std::uint32_t{type.columns},
                               std::uint32_t{type.components});
    }

    return type.components > 1u ? std::format("{}vec{}", prefix, std::uint32_t{type.components})
                                : scalar;
}

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
