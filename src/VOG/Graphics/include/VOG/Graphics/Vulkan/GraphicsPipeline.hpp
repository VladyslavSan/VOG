#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

namespace VOG::Graphics::Vulkan
{
class Device;

using ColorComponent = vk::ColorComponentFlagBits;
using vk::DynamicState;
using vk::FrontFace;
using vk::PolygonMode;
using vk::PrimitiveTopology;
using CullMode = vk::CullModeFlagBits;

struct RasterizationOptions
{
    // PipelineInputAssemblyStateCreateInfo
    PrimitiveTopology topology              : 4 = PrimitiveTopology::eTriangleList;
    // PipelineRasterizationStateCreateInfo
    bool        rasterizationDiscardEnabled : 1 = false;
    PolygonMode polygonMode                 : 2 = PolygonMode::eFill;
    CullMode    cullMode                    : 2 = CullMode::eBack;
    FrontFace   frontFace                   : 1 = FrontFace::eCounterClockwise;
    bool        primitiveRestartEnabled     : 1 = false;
};

using DepthStencilState = vk::PipelineDepthStencilStateCreateInfo;
using MultisampleState  = vk::PipelineMultisampleStateCreateInfo;
using ViewportState     = vk::PipelineViewportStateCreateInfo;
using DynamicStates     = StaticVector<vk::DynamicState, Limits::gMaxNumDynamicStates>;

class GraphicsPipeline : public vk::raii::Pipeline
{
public:
    struct ColorBlendState
    {
        StaticVector<vk::PipelineColorBlendAttachmentState, Limits::gMaxNumAttachments> attachments;
    };

    struct CreateInfo
    {
        const Device&                               device;
        vk::Optional<const vk::raii::PipelineCache> cache;
        ShaderProgram                               shading;
        VertexLayout                                vertexLayout;
        RasterizationOptions                        rasterizer;
        ViewportState                               viewportState;
        DepthStencilState                           depthStencil;
        ColorBlendState                             blending;
        MultisampleState                            multisample;
        DynamicStates                               dynamicStates;
        const vk::PipelineLayout&                   pipelineLayout;
        const RenderPass&                           renderPass;
        std::uint32_t                               subpass;
    };

    struct CreateInfoFromDescription
    {
        const Device&                               device;
        vk::Optional<const vk::raii::PipelineCache> cache;
        ShaderProgram                               shading;
        VertexLayout                                vertexLayout;
        RasterizationOptions                        rasterizer;
        ViewportState                               viewportState;
        DepthStencilState                           depthStencil;
        ColorBlendState                             blending;
        MultisampleState                            multisample;
        DynamicStates                               dynamicStates;
        const vk::PipelineLayout&                   pipelineLayout;
        RenderpassDescription                       renderpassDescription;
    };

    GraphicsPipeline(const CreateInfo& createInfo);

    GraphicsPipeline(const CreateInfoFromDescription& createInfo);

    const RenderpassDescription renderpassDescription;
};
} // namespace VOG::Graphics::Vulkan
