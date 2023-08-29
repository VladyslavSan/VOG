#include "VOG/Graphics/Vulkan/ShaderProgram.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>

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
            if (auto* uniformBufferPtr =
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
} // namespace

ShaderProgram::ShadingStagesChecked::ShadingStagesChecked(
    ShaderProgram::ShadingStages shadingStages)
    : ShaderProgram::ShadingStages{std::move(shadingStages)}
{
    VOG_ASSERT(vertex.shader != nullptr &&
               vertex.shader->stage == vk::ShaderStageFlagBits::eVertex);
    VOG_ASSERT(fragment.shader != nullptr &&
               fragment.shader->stage == vk::ShaderStageFlagBits::eFragment);

    if (!vertex.shader || !fragment.shader)
    {
        throw std::runtime_error{"Shader program is incomplete."};
    }
}

ShaderProgram::ShaderProgram(ShaderProgram::ShadingStagesChecked _stages)
    : stages{std::move(_stages)}
    , descriptorSets{buildDescriptorSets(*stages.vertex.shader->device, stages)}
{
}
} // namespace VOG::Graphics::Vulkan