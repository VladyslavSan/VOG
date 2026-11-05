#include "VOG/Engine/Renderer/PipelineTable.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <algorithm>
#include <utility>

namespace VOG::Engine
{
namespace
{
vk::PipelineColorBlendAttachmentState
blendOf(BlendMode mode)
{
    constexpr auto writeMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    switch (mode)
    {
    case BlendMode::eAlpha:
        return {.blendEnable         = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eOne,
                .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = writeMask};
    case BlendMode::eAdditive:
        // The color adds to what is there, weighted by the alpha of the draw; the alpha stays.
        return {.blendEnable         = vk::True,
                .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
                .dstColorBlendFactor = vk::BlendFactor::eOne,
                .colorBlendOp        = vk::BlendOp::eAdd,
                .srcAlphaBlendFactor = vk::BlendFactor::eZero,
                .dstAlphaBlendFactor = vk::BlendFactor::eOne,
                .alphaBlendOp        = vk::BlendOp::eAdd,
                .colorWriteMask      = writeMask};
    case BlendMode::eOpaque:
        break;
    }

    return {.blendEnable = vk::False, .colorWriteMask = writeMask};
}

Graphics::Vulkan::GraphicsPipelinePtr
makePipeline(Graphics::Vulkan::Device&   device,
             const Detail::MeshData&     mesh,
             const Detail::MaterialData& material,
             vk::Format                  colorFormat,
             vk::Format                  depthFormat)
{
    Graphics::Vulkan::VertexLayout vertexLayout{
        .bindingDescription = {{.binding   = 0u,
                                .stride    = mesh.layout.stride,
                                .inputRate = vk::VertexInputRate::eVertex}}};
    std::uint32_t location = 0u;
    for (const VertexAttribute& attribute : mesh.layout.inputs())
    {
        vertexLayout.attributeDescription.push_back({.location = location++,
                                                     .binding  = 0u,
                                                     .format   = toVk(attribute.format),
                                                     .offset   = attribute.offset});
    }

    const RenderState& state = material.state;

    return device.createGraphicsPipeline(Graphics::Vulkan::GraphicsPipeline::Parameters{
        .shading              = material.shader->program,
        .vertexLayout         = std::move(vertexLayout),
        .rasterizer           = {.cullMode  = state.cull == CullMode::eBack
                                                ? Graphics::Vulkan::CullMode::eBack
                                                : Graphics::Vulkan::CullMode::eNone,
                                 .frontFace = Graphics::Vulkan::FrontFace::eClockwise},
        .viewportState        = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil         = {.depthTestEnable = vk::True,
                                 .depthWriteEnable =
                                     state.depth == DepthMode::eTestAndWrite ? vk::True : vk::False,
                                 .depthCompareOp = vk::CompareOp::eLess},
        .blending             = {.attachments = {blendOf(state.blend)}},
        .multisample          = {},
        .dynamicStates        = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {colorFormat},
                                 .depthAttachmentFormat  = depthFormat}});
}
} // namespace

PipelineTable::PipelineTable(Graphics::Vulkan::DevicePtr device)
    : mDevice{std::move(device)}
{
}

std::uint32_t
PipelineTable::resolve(const Detail::MeshData&     mesh,
                       const Detail::MaterialData& material,
                       vk::Format                  colorFormat,
                       vk::Format                  depthFormat)
{
    const Key key{.programId   = material.shader->programId,
                  .layoutId    = mesh.layoutId,
                  .state       = material.state,
                  .colorFormat = colorFormat,
                  .depthFormat = depthFormat};
    if (const auto found = mIndices.find(key); found != mIndices.end())
    {
        return found->second;
    }

    mPipelines.push_back(makePipeline(*mDevice, mesh, material, colorFormat, depthFormat));
    const auto index = static_cast<std::uint32_t>(mPipelines.size() - 1u);
    mIndices.emplace(key, index);

    return index;
}

const Graphics::Vulkan::GraphicsPipelinePtr&
PipelineTable::at(std::uint32_t index) const
{
    return mPipelines.at(index);
}
} // namespace VOG::Engine
