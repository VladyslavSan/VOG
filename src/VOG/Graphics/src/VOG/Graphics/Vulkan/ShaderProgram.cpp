#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>

#include <algorithm>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{
using BlockMember = Shader::Reflection::BlockMember;
using HeapBlock   = Shader::Reflection::HeapBlock;

/** The byte intervals of the two members intersect; symmetric, so the stage order does not matter.
 */
bool
overlap(const BlockMember& left, const BlockMember& right)
{
    return (left.offset < right.offset + right.size && right.offset < left.offset + left.size) ||
           left.offset == right.offset;
}

/**
 * Adds the members of @p from that are in @p merged already, as an identical member, or that
 * conflict with none of them. A member conflicts with one that has its name, or that overlaps it
 * in bytes, and is not identical. @p what names the block in the error.
 */
void
mergeMembers(std::vector<BlockMember>&    merged,
             std::span<const BlockMember> from,
             std::string_view             what)
{
    for (const BlockMember& member : from)
    {
        bool present = false;
        for (const BlockMember& existing : merged)
        {
            if (existing == member)
            {
                present = true;
            }
            else if (existing.name == member.name) [[unlikely]]
            {
                throw std::runtime_error{
                    std::format("The vertex and fragment shaders declare the {} \"{}\" "
                                "differently: at offset {} in one stage and at {} in the other, "
                                "or with another type.",
                                what,
                                member.name,
                                existing.offset,
                                member.offset)};
            }
            else if (overlap(existing, member)) [[unlikely]]
            {
                throw std::runtime_error{std::format(
                    "The vertex and fragment shaders declare the {0} \"{1}\" and the {0} \"{2}\" "
                    "over the same bytes.",
                    what,
                    existing.name,
                    member.name)};
            }
        }
        if (!present)
        {
            merged.push_back(member);
        }
    }
    std::ranges::stable_sort(merged, std::ranges::less{}, &BlockMember::offset);
}

std::vector<BlockMember>
buildPushConstants(const ShaderProgram::ShadingStages& stages)
{
    std::vector<BlockMember> pushConstants;
    for (const auto& stage : {stages.vertex, stages.fragment})
    {
        mergeMembers(pushConstants, stage.shader->reflection.pushConstants, "push member");
    }

    return pushConstants;
}

/** The descriptor heap places resource i at byte i * kSlotSize; a block read otherwise is wrong. */
void
requireSlotStride(const HeapBlock& block, std::string_view stageName)
{
    if (block.strideDecoration == DescriptorHeapLayout::kSlotSize)
    {
        return;
    }

    throw std::runtime_error{
        std::format("The {} shader reads the heap block \"{}\" with {}; the descriptor heap places "
                    "resources {} bytes apart (DescriptorHeapLayout::kSlotSize).",
                    stageName,
                    block.name,
                    block.strideDecoration == 0u
                        ? std::string{"the size of a descriptor (no descriptor_stride)"}
                        : std::format("a stride of {} bytes", block.strideDecoration),
                    DescriptorHeapLayout::kSlotSize)};
}

std::vector<HeapBlock>
buildHeapBlocks(const ShaderProgram::ShadingStages& stages)
{
    std::map<std::string, HeapBlock> merged;
    for (const auto& [stage, stageName] :
         {std::pair{stages.vertex, "vertex"}, std::pair{stages.fragment, "fragment"}})
    {
        for (const HeapBlock& block : stage.shader->reflection.heapBlocks)
        {
            // Per stage, so that a stage whose stride is wrong cannot hide behind the other one.
            requireSlotStride(block, stageName);

            const auto [found, isNew] = merged.try_emplace(block.name, block);
            if (isNew)
            {
                std::ranges::stable_sort(
                    found->second.members, std::ranges::less{}, &BlockMember::offset);
                continue;
            }

            HeapBlock& existing = found->second;
            if (existing.storage != block.storage) [[unlikely]]
            {
                throw std::runtime_error{std::format(
                    "The vertex and fragment shaders declare the block \"{}\" as a uniform block "
                    "in one stage and a storage block in the other.",
                    block.name)};
            }
            existing.size = std::max(existing.size, block.size);
            mergeMembers(existing.members,
                         block.members,
                         std::format("member of the block \"{}\"", block.name));
        }
    }

    std::vector<HeapBlock> blocks;
    blocks.reserve(merged.size());
    std::ranges::move(merged | std::views::values, std::back_inserter(blocks));

    return blocks;
}

/** How a message names a block: in the middle of a sentence, and at the start of one. */
struct BlockName
{
    std::string reference;
    std::string subject;
};

/** The names for the heap block @p heapBlockName, or for the push block if that is empty. */
BlockName
describeBlock(std::string_view heapBlockName)
{
    if (heapBlockName.empty())
    {
        return {.reference = "the push block", .subject = "The push block"};
    }

    return {.reference = std::format("the block \"{}\"", heapBlockName),
            .subject   = std::format("The block \"{}\"", heapBlockName)};
}

/** What a layout cannot describe about @p member: "an array", "a struct", "a row_major matrix"; or
 *  empty for a scalar, a vector or a column-major matrix. */
std::string_view
undescribableKind(const BlockMember& member)
{
    if (member.rowMajor)
    {
        return "a row_major matrix";
    }
    if (!member.structName.empty())
    {
        return "a struct";
    }
    if (member.arraySize != 0u || member.arrayStride != 0u)
    {
        return "an array";
    }

    return {};
}

/** Throws unless the reflected @p member of @p block is what the C++ member @p cpp is. */
void
requireSameMember(const std::string&         block,
                  const BlockMember&         member,
                  const BlockLayout::Member& cpp)
{
    if (const std::string_view kind = undescribableKind(member); !kind.empty())
    {
        throw std::invalid_argument{std::format(
            "Member \"{}\" of {} is {}; a C++ layout checks scalar, vector and matrix members "
            "only.",
            member.name,
            block,
            kind)};
    }
    if (member.offset != cpp.offset)
    {
        throw std::invalid_argument{
            std::format("Member \"{}\" of {} is at offset {} in the shader and {} in C++.",
                        member.name,
                        block,
                        member.offset,
                        cpp.offset)};
    }
    if (member.type != cpp.type.type)
    {
        throw std::invalid_argument{
            std::format("Member \"{}\" of {} has the type {} in the shader and {} in C++.",
                        member.name,
                        block,
                        Shader::describe(member.type),
                        Shader::describe(cpp.type.type))};
    }
    if (member.matrixStride != cpp.type.matrixStride)
    {
        throw std::invalid_argument{std::format(
            "Member \"{}\" of {} has matrix columns {} bytes apart in the shader and {} in "
            "C++ (a column of 3 components is not padded in C++ as it is in GLSL).",
            member.name,
            block,
            member.matrixStride,
            cpp.type.matrixStride)};
    }
}

/**
 * Checks the reflected @p members of a block, in offset order, against @p layout. With @p exact the
 * layout may have no member that @p members lack. @p blockName is empty for the push block.
 */
void
compareBlock(std::string_view             blockName,
             std::span<const BlockMember> members,
             const BlockLayout&           layout,
             bool                         exact)
{
    const auto [block, subject] = describeBlock(blockName);

    for (const BlockMember& member : members)
    {
        const auto cpp = std::ranges::find(layout.members, member.name, &BlockLayout::Member::name);
        if (cpp == layout.members.end())
        {
            throw std::invalid_argument{std::format(
                "{} declares \"{}\" ({} at offset {}), which the C++ layout lacks.",
                subject,
                member.name,
                member.structName.empty() ? Shader::describe(member.type) : member.structName,
                member.offset)};
        }

        requireSameMember(block, member, *cpp);
    }

    if (!exact)
    {
        return;
    }
    for (const BlockLayout::Member& cpp : layout.members)
    {
        if (std::ranges::find(members, cpp.name, &BlockMember::name) == members.end())
        {
            throw std::invalid_argument{
                std::format("The C++ layout declares \"{}\" at offset {}, which {} lacks.",
                            cpp.name,
                            cpp.offset,
                            block)};
        }
    }
}
} // namespace

ShaderProgram::ShadingStagesChecked::ShadingStagesChecked(
    ShaderProgram::ShadingStages shadingStages)
    : ShaderProgram::ShadingStages{std::move(shadingStages)}
{
    if (!vertex.shader || !fragment.shader)
    {
        throw std::runtime_error{"Shader program is incomplete."};
    }

    VOG_ASSERT(vertex.shader != nullptr &&
               vertex.shader->stage == vk::ShaderStageFlagBits::eVertex);
    VOG_ASSERT(fragment.shader != nullptr &&
               fragment.shader->stage == vk::ShaderStageFlagBits::eFragment);
}

std::unique_ptr<ShaderProgram>
ShaderProgram::create(ShadingStages stages)
{
    return std::unique_ptr<ShaderProgram>(
        new ShaderProgram{ShadingStagesChecked{std::move(stages)}});
}

ShaderProgram::ShaderProgram(ShaderProgram::ShadingStagesChecked _stages)
    : stages{std::move(_stages)}
    , pushConstants{buildPushConstants(stages)}
    , heapBlocks{buildHeapBlocks(stages)}
{
}

void
ShaderProgram::requireHeapBlock(std::string_view name, const BlockLayout& layout) const
{
    const auto block = std::ranges::find(heapBlocks, name, &HeapBlock::name);
    if (block == heapBlocks.end())
    {
        throw std::invalid_argument{std::format("The program reads no heap block \"{}\".", name)};
    }

    compareBlock(name, block->members, layout, true);
}

void
ShaderProgram::requirePushBlock(const BlockLayout& layout) const
{
    compareBlock({}, pushConstants, layout, false);
}
} // namespace VOG::Graphics::Vulkan
