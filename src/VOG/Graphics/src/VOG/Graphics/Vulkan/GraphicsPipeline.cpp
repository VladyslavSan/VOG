#include "VOG/Graphics/Vulkan/GraphicsPipeline.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
namespace
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
                         {
                             return location == description.location;
                         });

        if (found == vertexLayout.attributeDescription.end() ||
            !Shader::vertexFormatCompatible(vertexAttribute.format, found->format))
        {
            return false;
        }
    }

    return true;
}

vk::raii::Pipeline
makePipeline(const Device&                       device,
             const ShaderProgram&                program,
             const GraphicsPipeline::Parameters& createInfo)
{
    using ShadingStateInfo = vk::PipelineShaderStageCreateInfo;

    std::array shadingStages = {
        ShadingStateInfo{
            .stage  = vk::ShaderStageFlagBits::eVertex,
            .module = *program.stages.vertex.shader->module,
            .pName  = program.stages.vertex.entryPoint,
        },
        ShadingStateInfo{
            .stage  = vk::ShaderStageFlagBits::eFragment,
            .module = *program.stages.fragment.shader->module,
            .pName  = program.stages.fragment.entryPoint,
        },
    };

    const auto& vertexLayout = createInfo.vertexLayout;

    const vk::PipelineVertexInputStateCreateInfo vertexInputState{
        .vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.bindingDescription.size()),
        .pVertexBindingDescriptions = vertexLayout.bindingDescription.data(),
        .vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.attributeDescription.size()),
        .pVertexAttributeDescriptions = vertexLayout.attributeDescription.data()};

    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .topology               = createInfo.rasterizer.topology,
        .primitiveRestartEnable = createInfo.rasterizer.primitiveRestartEnabled};

    const vk::PipelineRasterizationStateCreateInfo rasterizationState{
        .depthClampEnable        = 0u,
        .rasterizerDiscardEnable = createInfo.rasterizer.rasterizationDiscardEnabled,
        .polygonMode             = createInfo.rasterizer.polygonMode,
        .cullMode                = createInfo.rasterizer.cullMode,
        .frontFace               = createInfo.rasterizer.frontFace,
        .depthBiasEnable         = 0u,
        .lineWidth               = 1.0f};

    const vk::PipelineColorBlendStateCreateInfo blendingState = {
        .attachmentCount = static_cast<std::uint32_t>(createInfo.blending.attachments.size()),
        .pAttachments    = createInfo.blending.attachments.data()};

    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>(createInfo.dynamicStates.size()),
        .pDynamicStates    = createInfo.dynamicStates.data()};

    vk::StructureChain info{
        vk::GraphicsPipelineCreateInfo{
            .stageCount          = static_cast<std::uint32_t>(shadingStages.size()),
            .pStages             = shadingStages.data(),
            .pVertexInputState   = &vertexInputState,
            .pInputAssemblyState = &inputAssemblyState,
            .pViewportState      = &createInfo.viewportState,
            .pRasterizationState = &rasterizationState,
            .pMultisampleState   = &createInfo.multisample,
            .pDepthStencilState  = &createInfo.depthStencil,
            .pColorBlendState    = &blendingState,
            .pDynamicState       = &dynamicState,
            .layout              = *program.pipelineLayout,
        },
        vk::PipelineRenderingCreateInfo{
            .colorAttachmentCount = static_cast<std::uint32_t>(
                createInfo.renderingDescription.colorAttachmentFormats.size()),
            .pColorAttachmentFormats =
                createInfo.renderingDescription.colorAttachmentFormats.data(),
            .depthAttachmentFormat   = createInfo.renderingDescription.depthAttachmentFormat,
            .stencilAttachmentFormat = createInfo.renderingDescription.stencilAttachmentFormat,
        }};

    try
    {
        return vk::raii::Pipeline{device.raii(), device.pipelineCache, info.get()};
    }
    catch (const vk::SystemError& error)
    {
        using namespace std::string_literals;
        if (!validateVertexAttributes(program, vertexLayout))
        {
            throw std::runtime_error{
                "GraphicsPipeline creation failed. Shader program is incompatible with vertex "
                "layout provided."};
        }
        throw std::runtime_error{"GraphicsPipeline creation failed. "s + error.what()};
    }
}

const ShaderProgram&
requireProgram(const GraphicsPipeline::Parameters& createInfo)
{
    VOG_ASSERT_MSG(createInfo.shading, "A graphics pipeline needs a shader program.");

    return *createInfo.shading;
}
} // namespace

GraphicsPipeline::GraphicsPipeline(DevicePtr device, Parameters createInfo)
    : DeviceChild{std::move(device)}
    , vk::raii::Pipeline{makePipeline(
          *DeviceChild::device(), requireProgram(createInfo), createInfo)}
    , renderingDescription{createInfo.renderingDescription}
    , program{std::move(createInfo.shading)}
{
}
} // namespace VOG::Graphics::Vulkan
