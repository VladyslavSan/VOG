#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

#include <algorithm>
#include <ranges>
#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
{
ShaderProgram::DescriptorSets
buildDescriptorSets(const Device& device, const ShaderProgram::ShadingStages& stages)
{
    ShaderProgram::DescriptorSets descriptorSets{};

    auto handleBinding = [](ShaderProgram::BindingDescription&       bindingDescription,
                            const Shader::Reflection::UniformBuffer& resource,
                            vk::ShaderStageFlags                     stage)
    {
        using ResourceType = std::remove_cvref<decltype(resource)>::type;
        if (bindingDescription.resource.index() == 0u)
        {
            bindingDescription.resource.emplace<ResourceType>(resource);
            bindingDescription.stageFlags = stage;

            return;
        }

        if (std::holds_alternative<ResourceType>(bindingDescription.resource) &&
            std::get<ResourceType>(bindingDescription.resource) == resource)
        {
            bindingDescription.stageFlags |= stage;
        }
        else
        {
            throw std::runtime_error{"Resource binding incompatibility between stages."};
        }
    };

    for (const auto& stage : {stages.vertex, stages.fragment})
    {
        for (const auto& uniformBuffer : stage.shader->reflection.uniformBuffers)
        {
            const auto& location        = uniformBuffer.location;
            auto&       bindingResource = descriptorSets[location.set].resources[location.binding];

            handleBinding(bindingResource, uniformBuffer, stage.shader->stage);
        }
    }

    for (auto& set : descriptorSets)
    {
        StaticVector<vk::DescriptorSetLayoutBinding, Limits::gMaxNumDescriptorBindings> bindings;
        for (const auto& bindingDescription : set.resources)
        {
            if (bindingDescription.resource.index() == 0u)
            {
                continue;
            }

            vk::DescriptorSetLayoutBinding setLayoutBinding;
            setLayoutBinding.stageFlags      = bindingDescription.stageFlags;
            setLayoutBinding.descriptorCount = 1u;
            if (const auto* uniformBufferPtr =
                    std::get_if<Shader::Reflection::UniformBuffer>(&bindingDescription.resource))
            {
                setLayoutBinding.binding        = uniformBufferPtr->location.binding;
                setLayoutBinding.descriptorType = vk::DescriptorType::eUniformBuffer;
            }

            bindings.push_back(setLayoutBinding);
        }

        if (!bindings.empty())
        {
            set.setLayout = vk::raii::DescriptorSetLayout{
                device,
                vk::DescriptorSetLayoutCreateInfo{
                    .bindingCount = static_cast<std::uint32_t>(bindings.size()),
                    .pBindings    = bindings.data(),
                }};
        }
    }

    return descriptorSets;
}

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
                [&pushConstant, &valueInRange](const vk::PushConstantRange& range)
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
                    .stageFlags = stage.shader->stage,
                    .offset     = pushConstant.offset,
                    .size       = pushConstant.size,
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

                foundOverlap->stageFlags |= stage.shader->stage;
            }
        }
    }

    return pushConstants;
}

vk::raii::PipelineLayout
makePipelineLayout(const Device&                        device,
                   const ShaderProgram::DescriptorSets& descriptorSets,
                   const ShaderProgram::PushConstants&  pushConstants)
{
    // Find the highest set index that is actually used. Sets with gaps below
    // it must still appear in pSetLayouts (as empty layouts) so that Vulkan
    // sees a contiguous, correctly-indexed array.
    std::int32_t lastUsedSet = -1;
    for (std::int32_t i = 0; i < static_cast<std::int32_t>(descriptorSets.size()); ++i)
    {
        if (*descriptorSets[static_cast<std::size_t>(i)].setLayout)
        {
            lastUsedSet = i;
        }
    }

    // Temporary empty layouts kept alive for the duration of the API call.
    std::vector<vk::raii::DescriptorSetLayout>                                 emptyLayouts;
    StaticVectorStrict<vk::DescriptorSetLayout, Limits::gMaxNumDescriptorSets> descriptorSetLayouts;
    for (std::int32_t i = 0; i <= lastUsedSet; ++i)
    {
        const auto& set = descriptorSets[static_cast<std::size_t>(i)];
        if (*set.setLayout)
        {
            descriptorSetLayouts.push_back(*set.setLayout);
        }
        else
        {
            emptyLayouts.emplace_back(device, vk::DescriptorSetLayoutCreateInfo{});
            descriptorSetLayouts.push_back(*emptyLayouts.back());
        }
    }

    return {device,
            {
                .setLayoutCount         = static_cast<std::uint32_t>(descriptorSetLayouts.size()),
                .pSetLayouts            = descriptorSetLayouts.data(),
                .pushConstantRangeCount = static_cast<std::uint32_t>(pushConstants.ranges.size()),
                .pPushConstantRanges    = pushConstants.ranges.data(),
            }};
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

ShaderProgram::ShaderProgram(ShaderProgram::ShadingStagesChecked _stages)
    : device{_stages.vertex.shader->device}
    , stages{std::move(_stages)}
    , descriptorSets{buildDescriptorSets(*device, stages)}
    , pushConstants{buildPushConstants(stages)}
    , pipelineLayout{makePipelineLayout(*device, descriptorSets, pushConstants)}
{
}
} // namespace VOG::Graphics::Vulkan
