#pragma once

#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/BlockLayout.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
class ShaderProgram
{
public:
    struct ShadingStageInfo
    {
        ShaderPtr   shader     = nullptr;
        const char* entryPoint = "main";
    };

    struct ShadingStages
    {
        ShadingStageInfo vertex;
        ShadingStageInfo fragment;
    };

    struct ShadingStagesChecked : ShadingStages
    {
        ShadingStagesChecked(ShadingStages shadingStages);
    };

    /**
     * Links vertex and fragment shaders into a shader program, merging the push members and the
     * heap blocks of both stages: members that have the same name, or that overlap in bytes, must
     * be identical, and a member that does neither is added. A stage that declares no push block,
     * or reads no heap block, contributes nothing.
     *
     * @throws std::runtime_error if a stage is missing, the stages declare members of the push
     *         block or of one heap block that share a name or overlap, and differ, a heap block is
     *         a uniform block in one stage and a storage block in the other, or a heap block has a
     *         stride other than DescriptorHeapLayout::kSlotSize.
     */
    [[nodiscard]] static std::unique_ptr<ShaderProgram> create(ShadingStages stages);

    /**
     * Checks that the heap block @p name declares exactly the members of @p layout: the same
     * names, offsets, types and matrix strides, nothing more and nothing less. A member the shader
     * declares and never reads is still reflected, so a C++ member beside it is a real
     * disagreement. The first mismatch in offset order is reported, naming the block and the
     * member.
     *
     * @throws std::invalid_argument if the program reads no heap block @p name, or the block
     *         and @p layout differ. An array, a struct or a row-major matrix member is a
     *         difference too: a layout describes scalars, vectors and matrices.
     */
    void requireHeapBlock(std::string_view name, const BlockLayout& layout) const;

    /**
     * Checks that every push member a stage declares is in @p layout with the same offset, type
     * and matrix stride. Members of @p layout that no stage declares are allowed: stages declare
     * subsets, and pushing bytes no stage reads is harmless.
     *
     * @throws std::invalid_argument if a push member differs from the one of @p layout of its
     *         name or has none, naming the first such member in offset order.
     */
    void requirePushBlock(const BlockLayout& layout) const;

private:
    explicit ShaderProgram(ShadingStagesChecked stages);

public:
    /** The shaders in the stages keep the device alive; the program owns no Vulkan object. */
    const ShadingStages stages;

    /** The push members of both stages, merged, sorted by offset. */
    const std::vector<Shader::Reflection::BlockMember> pushConstants;

    /** The heap blocks of both stages, merged by name and sorted by name. Their members are sorted
     *  by offset. */
    const std::vector<Shader::Reflection::HeapBlock> heapBlocks;
};
} // namespace VOG::Graphics::Vulkan
