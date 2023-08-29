#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <array>

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
     * Binding resource can be one of the listed types.
     */
    using BindingResource = std::variant<std::monostate, Shader::Reflection::UniformBuffer>;

    /**
     * Describes the set binding.
     */
    struct BindingDescription
    {
        BindingResource      resource;
        vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    };

    /**
     * Holds info about a single descriptor set layout.
     */
    struct SetLayout
    {
        /* Vulkan's descriptor set layout object created from @p resources. */
        vk::raii::DescriptorSetLayout setLayout = nullptr;
        /* Binding resources of a set. */
        std::array<BindingDescription, Limits::gMaxNumDescriptorBindings> resources;
    };

    using DescriptorSets = std::array<SetLayout, Limits::gMaxNumDescriptorSets>;

    ShaderProgram(ShadingStagesChecked stages);

public:
    const ShadingStages  stages;
    const DescriptorSets descriptorSets;
};
} // namespace VOG::Graphics::Vulkan
