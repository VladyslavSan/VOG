#include "VOG/Graphics/Vulkan/GraphicsPipeline.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>

namespace VOG::Graphics::Vulkan
{
namespace
{
/** "vec3", "ivec4", "mat4", "float": how the shader declares the input. */
std::string
describe(Shader::Reflection::BaseType type)
{
    using Type = Shader::Reflection::BaseType::Type;

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

/**
 * Every location the vertex shader reads needs an attribute of a compatible numeric type, or the
 * pipeline is invalid (VUID-VkGraphicsPipelineCreateInfo-Input-07904, -08733). Extra attributes
 * are allowed, and so are attributes with fewer or more components than the shader declares.
 */
void
validateVertexInputs(const ShaderProgram& program, const VertexLayout& vertexLayout)
{
    for (const auto& input : program.stages.vertex.shader->reflection.inAttributes)
    {
        for (std::uint32_t location = input.location;
             location < input.location + input.locationCount;
             ++location)
        {
            const auto found = std::ranges::find(vertexLayout.attributeDescription,
                                                 location,
                                                 &vk::VertexInputAttributeDescription::location);
            if (found == vertexLayout.attributeDescription.end())
            {
                throw std::invalid_argument{
                    std::format("The vertex shader reads \"{}\" ({}) at location {}, but the "
                                "vertex layout has no attribute there.",
                                input.name,
                                describe(input.format),
                                location)};
            }
            if (!Shader::vertexFormatCompatible(input.format, found->format))
            {
                throw std::invalid_argument{
                    std::format("The vertex shader reads \"{}\" ({}) at location {}, which the "
                                "vertex attribute format {} cannot feed.",
                                input.name,
                                describe(input.format),
                                location,
                                vk::to_string(found->format))};
            }
        }
    }
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
    validateVertexInputs(program, vertexLayout);

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

    // No pipeline layout: the shaders reach their resources through the descriptor heap.
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
            .layout              = nullptr,
        },
        vk::PipelineCreateFlags2CreateInfo{.flags =
                                               vk::PipelineCreateFlagBits2::eDescriptorHeapEXT},
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
