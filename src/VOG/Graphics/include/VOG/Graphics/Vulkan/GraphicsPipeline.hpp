#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
VOG_DECLARE_PTR(ShaderProgram);

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

    struct Parameters
    {
        vk::Optional<const vk::raii::PipelineCache> cache;
        ShaderProgramPtr                            shading;
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

private:
    friend class Device;

    explicit GraphicsPipeline(DevicePtr device, Parameters createInfo);

public:
    const RenderpassDescription renderpassDescription;
    const DevicePtr             device;
    const ShaderProgramPtr      program;
};
} // namespace VOG::Graphics::Vulkan
