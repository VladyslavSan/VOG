#include "VOG/Engine/Renderer/PipelineCache.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <algorithm>
#include <utility>

namespace VOG::Engine
{
namespace
{
using namespace Graphics::Vulkan;

GraphicsPipelinePtr
makePipeline(Device& device, const Mesh& mesh, const Material& material, vk::Format colorFormat)
{
    VertexLayout vertexLayout{
        .bindingDescription = {
            {.binding = 0u, .stride = mesh.stride, .inputRate = vk::VertexInputRate::eVertex}}};
    for (std::uint32_t location = 0u; location < mesh.attributes.size(); ++location)
    {
        const VertexAttribute& attribute = mesh.attributes[location];
        vertexLayout.attributeDescription.push_back({.location = location,
                                                     .binding  = 0u,
                                                     .format   = toVk(attribute.format),
                                                     .offset   = attribute.offset});
    }

    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    return device.createGraphicsPipeline(GraphicsPipeline::Parameters{
        .shading       = material.program,
        .vertexLayout  = std::move(vertexLayout),
        .rasterizer    = {.cullMode = CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {.depthTestEnable = 0u},
        .blending      = {.attachments = {{.blendEnable = 0u, .colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {colorFormat}}});
}
} // namespace

PipelineCache::PipelineCache(Graphics::Vulkan::DevicePtr device)
    : mDevice{std::move(device)}
{
}

const Graphics::Vulkan::GraphicsPipelinePtr&
PipelineCache::get(const Mesh& mesh, const Material& material, vk::Format colorFormat)
{
    const auto found = std::ranges::find_if(
        mEntries,
        [&](const Entry& entry)
        {
            return entry.pipeline->program == material.program && entry.stride == mesh.stride &&
                   entry.colorFormat == colorFormat && entry.attributes == mesh.attributes;
        });
    if (found != mEntries.end())
    {
        return found->pipeline;
    }

    return mEntries
        .emplace_back(Entry{.stride      = mesh.stride,
                            .attributes  = mesh.attributes,
                            .colorFormat = colorFormat,
                            .pipeline    = makePipeline(*mDevice, mesh, material, colorFormat)})
        .pipeline;
}
} // namespace VOG::Engine
