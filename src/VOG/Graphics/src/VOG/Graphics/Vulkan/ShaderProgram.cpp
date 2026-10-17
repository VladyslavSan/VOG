#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <VOG/Common/Assert.hpp>

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
ShaderProgram::PushConstants
buildPushConstants(const ShaderProgram::ShadingStages& stages)
{
    ShaderProgram::PushConstants pushConstants;

    auto valueInRange = [](const auto left, const auto right, const auto value) -> bool
    {
        return left <= value && value <= right;
    };

    for (const auto& stage : {stages.vertex, stages.fragment})
    {
        for (const auto& pushConstant : stage.shader->reflection.pushConstants.variables)
        {
            auto foundOverlap = std::ranges::find_if(
                pushConstants.ranges,
                [&pushConstant, &valueInRange](const ShaderProgram::PushConstants::Range& range)
                {
                    const auto end = range.offset + range.size - 1u;
                    return valueInRange(range.offset, end, pushConstant.offset) ||
                           valueInRange(
                               range.offset, end, pushConstant.offset + pushConstant.size - 1u);
                });

            // Simple path, just add push constant to ranges.
            if (foundOverlap == pushConstants.ranges.end())
            {
                pushConstants.ranges.push_back({
                    .offset = pushConstant.offset,
                    .size   = pushConstant.size,
                });
                pushConstants.names.push_back(pushConstant.name);

                continue;
            }

            if (foundOverlap != pushConstants.ranges.end())
            {
                const bool compatible =
                    foundOverlap->offset == pushConstant.offset &&
                    foundOverlap->size == pushConstant.size &&
                    pushConstant.name ==
                        pushConstants
                            .names[std::distance(pushConstants.ranges.begin(), foundOverlap)];

                if (!compatible) [[unlikely]]
                {
                    throw std::runtime_error{"Push constants are incompatible"};
                }
            }
        }
    }

    return pushConstants;
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
{
}
} // namespace VOG::Graphics::Vulkan
