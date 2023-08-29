#include "VOG/Graphics/Vulkan/GraphicsPipeline.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
bool
validateVertexAttributes(const ShaderProgram& shaderProgram, const VertexLayout& vertexLayout)
{
    const auto& vertexAttributes = shaderProgram.stages.vertex.shader->reflection.inAttributes;
    if (vertexAttributes.size() != vertexLayout.attributeDescription.size()) [[unlikely]]
    {
        return false;
    }

    for (const auto& vertexAttribute : vertexAttributes)
    {
        const auto found =
            std::find_if(vertexLayout.attributeDescription.begin(),
                         vertexLayout.attributeDescription.end(),
                         [location = vertexAttribute.location](const auto& description)
                         { return location == description.location; });

        if (found == vertexLayout.attributeDescription.end() ||
            !Shader::vertexFormatCompatible(vertexAttribute.format, found->format))
        {
            return false;
        }
    }

    return true;
}

GraphicsPipeline::GraphicsPipeline(const CreateInfo& createInfo)
    : vk::raii::Pipeline{nullptr}
    , device{std::move(createInfo.device)}
    , renderpassDescription{createInfo.renderPass.getRenderpassDescription()}
{
    using ShadingStateInfo = vk::PipelineShaderStageCreateInfo;

    std::array shadingStages = {
        ShadingStateInfo{.stage  = vk::ShaderStageFlagBits::eVertex,
                         .module = *createInfo.shading->stages.vertex.shader->module,
                         .pName  = createInfo.shading->stages.vertex.entryPoint},
        ShadingStateInfo{.stage  = vk::ShaderStageFlagBits::eFragment,
                         .module = *createInfo.shading->stages.fragment.shader->module,
                         .pName  = createInfo.shading->stages.fragment.entryPoint}};

    const auto& vertexLayout  = createInfo.vertexLayout;
    const auto& rasterizer    = createInfo.rasterizer;
    const auto& blending      = createInfo.blending;
    const auto& dynamicStates = createInfo.dynamicStates;

    const vk::PipelineVertexInputStateCreateInfo vertexInputState{
        .vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.bindingDescription.size()),
        .pVertexBindingDescriptions = vertexLayout.bindingDescription.data(),
        .vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.attributeDescription.size()),
        .pVertexAttributeDescriptions = vertexLayout.attributeDescription.data()};

    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .topology               = rasterizer.topology,
        .primitiveRestartEnable = rasterizer.primitiveRestartEnabled};

    const vk::PipelineRasterizationStateCreateInfo rasterizationState{
        .depthClampEnable        = 0u,
        .rasterizerDiscardEnable = rasterizer.rasterizationDiscardEnabled,
        .polygonMode             = rasterizer.polygonMode,
        .cullMode                = rasterizer.cullMode,
        .frontFace               = rasterizer.frontFace,
        .depthBiasEnable         = 0u,
        .lineWidth               = 1.0f};

    const vk::PipelineColorBlendStateCreateInfo blendingState = {
        .attachmentCount = static_cast<std::uint32_t>(blending.attachments.size()),
        .pAttachments    = blending.attachments.data()};

    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>(createInfo.dynamicStates.size()),
        .pDynamicStates    = createInfo.dynamicStates.data()};

    vk::GraphicsPipelineCreateInfo info{.pVertexInputState   = &vertexInputState,
                                        .pInputAssemblyState = &inputAssemblyState,
                                        .pViewportState      = &createInfo.viewportState,
                                        .pRasterizationState = &rasterizationState,
                                        .pMultisampleState   = &createInfo.multisample,
                                        .pDepthStencilState  = &createInfo.depthStencil,
                                        .pColorBlendState    = &blendingState,
                                        .pDynamicState       = &dynamicState,
                                        .layout              = createInfo.pipelineLayout,
                                        .renderPass          = *createInfo.renderPass,
                                        .subpass             = createInfo.subpass};
    info.setStages(shadingStages);

    try
    {
        vk::raii::Pipeline pipeline{*device, createInfo.cache, info};
        static_cast<vk::raii::Pipeline&>(*this) = std::move(pipeline);
    }
    catch (const vk::SystemError& error)
    {
        using namespace std::string_literals;
        if (!validateVertexAttributes(*createInfo.shading, vertexLayout))
        {
            throw std::runtime_error{
                "GraphicsPipeline creation failed. Shader program is incompatible with vertex "
                "layout provided."};
        }
        throw std::runtime_error{"GraphicsPipeline creation failed. "s + error.what()};
    }
}

GraphicsPipeline::GraphicsPipeline(const CreateInfoFromDescription& createInfo)
    : vk::raii::Pipeline{nullptr}
    , renderpassDescription{createInfo.renderpassDescription}
{
    using ShadingStateInfo = vk::PipelineShaderStageCreateInfo;

    std::array shadingStages = {
        ShadingStateInfo{.stage  = vk::ShaderStageFlagBits::eVertex,
                         .module = *createInfo.shading->stages.vertex.shader->module,
                         .pName  = createInfo.shading->stages.vertex.entryPoint},
        ShadingStateInfo{.stage  = vk::ShaderStageFlagBits::eFragment,
                         .module = *createInfo.shading->stages.fragment.shader->module,
                         .pName  = createInfo.shading->stages.fragment.entryPoint}};

    const auto& vertexLayout  = createInfo.vertexLayout;
    const auto& rasterizer    = createInfo.rasterizer;
    const auto& blending      = createInfo.blending;
    const auto& dynamicStates = createInfo.dynamicStates;

    const vk::PipelineVertexInputStateCreateInfo vertexInputState{
        .vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.bindingDescription.size()),
        .pVertexBindingDescriptions = vertexLayout.bindingDescription.data(),
        .vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.attributeDescription.size()),
        .pVertexAttributeDescriptions = vertexLayout.attributeDescription.data()};

    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .topology               = rasterizer.topology,
        .primitiveRestartEnable = rasterizer.primitiveRestartEnabled};

    const vk::PipelineRasterizationStateCreateInfo rasterizationState{
        .depthClampEnable        = 0u,
        .rasterizerDiscardEnable = rasterizer.rasterizationDiscardEnabled,
        .polygonMode             = rasterizer.polygonMode,
        .cullMode                = rasterizer.cullMode,
        .frontFace               = rasterizer.frontFace,
        .depthBiasEnable         = 0u,
        .lineWidth               = 1.0f};

    const vk::PipelineColorBlendStateCreateInfo blendingState = {
        .attachmentCount = static_cast<std::uint32_t>(blending.attachments.size()),
        .pAttachments    = blending.attachments.data()};

    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>(createInfo.dynamicStates.size()),
        .pDynamicStates    = createInfo.dynamicStates.data()};

    const vk::PipelineRenderingCreateInfo renderpassInfo = {
        .colorAttachmentCount = static_cast<std::uint32_t>(
            createInfo.renderpassDescription.colorAttachmentFormats.size()),
        .pColorAttachmentFormats = createInfo.renderpassDescription.colorAttachmentFormats.data(),
        .depthAttachmentFormat   = createInfo.renderpassDescription.depthAttachmentFormat,
        .stencilAttachmentFormat = createInfo.renderpassDescription.stencilAttachmentFormat};

    vk::GraphicsPipelineCreateInfo info{.pVertexInputState   = &vertexInputState,
                                        .pInputAssemblyState = &inputAssemblyState,
                                        .pViewportState      = &createInfo.viewportState,
                                        .pRasterizationState = &rasterizationState,
                                        .pMultisampleState   = &createInfo.multisample,
                                        .pDepthStencilState  = &createInfo.depthStencil,
                                        .pColorBlendState    = &blendingState,
                                        .pDynamicState       = &dynamicState,
                                        .layout              = createInfo.pipelineLayout};
    info.setStages(shadingStages);
    info.pNext = &renderpassInfo;

    try
    {
        vk::raii::Pipeline pipeline{*device, createInfo.cache, info};
        static_cast<vk::raii::Pipeline&>(*this) = std::move(pipeline);
    }
    catch (const vk::SystemError& error)
    {
        using namespace std::string_literals;
        if (!validateVertexAttributes(*createInfo.shading, vertexLayout))
        {
            throw std::runtime_error{
                "GraphicsPipeline creation failed. Shader program is incompatible with vertex "
                "layout provided."};
        }
        throw std::runtime_error{"GraphicsPipeline creation failed. "s + error.what()};
    }
}
} // namespace VOG::Graphics::Vulkan
