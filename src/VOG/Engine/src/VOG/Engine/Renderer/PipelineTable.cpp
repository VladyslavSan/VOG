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
Graphics::Vulkan::GraphicsPipelinePtr
makePipeline(Graphics::Vulkan::Device&   device,
             const Detail::MeshData&     mesh,
             const Detail::MaterialData& material,
             vk::Format                  colorFormat)
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

    using Graphics::Vulkan::ColorComponent;
    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    return device.createGraphicsPipeline(Graphics::Vulkan::GraphicsPipeline::Parameters{
        .shading       = material.shader->program,
        .vertexLayout  = std::move(vertexLayout),
        .rasterizer    = {.cullMode = Graphics::Vulkan::CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {.depthTestEnable = 0u},
        .blending      = {.attachments = {{.blendEnable = 0u, .colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {colorFormat}}});
}
} // namespace

PipelineTable::PipelineTable(Graphics::Vulkan::DevicePtr device)
    : mDevice{std::move(device)}
{
}

std::uint32_t
PipelineTable::resolve(const Detail::MeshData&     mesh,
                       const Detail::MaterialData& material,
                       vk::Format                  colorFormat)
{
    const Key key{.programId   = material.shader->programId,
                  .layoutId    = mesh.layoutId,
                  .colorFormat = colorFormat};
    if (const auto found = mIndices.find(key); found != mIndices.end())
    {
        return found->second;
    }

    mPipelines.push_back(makePipeline(*mDevice, mesh, material, colorFormat));
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
