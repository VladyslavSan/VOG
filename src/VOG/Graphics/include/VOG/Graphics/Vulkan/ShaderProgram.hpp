#pragma once

#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <array>
#include <variant>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

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
        /** Resource of a binding. */
        BindingResource resource;
        /** Stages that use the binding. */
        vk::ShaderStageFlags stageFlags = vk::ShaderStageFlagBits::eAll;
    };

    /**
     * Holds info about a single descriptor set layout.
     */
    struct SetLayout
    {
        /** Vulkan's descriptor set layout object created from @p resources. */
        vk::raii::DescriptorSetLayout setLayout = nullptr;
        /** Bindings of a set. Mostly for reflection purposes. */
        std::array<BindingDescription, Limits::gMaxNumDescriptorBindings> resources;
    };

    /**
     * Descriptor sets holder type.
     *
     * Index in this array corresponds to a set index.
     * If SetLayout::setLayout is nullptr it means that set is not present in a program.
     * SetLayout::
     */
    using DescriptorSets = std::array<SetLayout, Limits::gMaxNumDescriptorSets>;

    struct PushConstants
    {
        std::vector<std::string>           names;
        std::vector<vk::PushConstantRange> ranges;
    };

private:
    friend class Device;

    ShaderProgram(DevicePtr device, ShadingStagesChecked stages);

    const DevicePtr device;

public:
    const ShadingStages            stages;
    const DescriptorSets           descriptorSets;
    const PushConstants            pushConstants;
    const vk::raii::PipelineLayout pipelineLayout;
};
} // namespace VOG::Graphics::Vulkan
