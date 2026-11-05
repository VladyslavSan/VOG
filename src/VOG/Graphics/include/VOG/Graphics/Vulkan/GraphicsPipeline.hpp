#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Common.hpp>
#include <VOG/Graphics/Vulkan/DeviceChild.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(ShaderProgram);

using ColorComponent = vk::ColorComponentFlagBits;
using vk::DynamicState;
using vk::FrontFace;
using vk::PolygonMode;
using vk::PrimitiveTopology;
using CullMode = vk::CullModeFlagBits;

/**
 * The enums of Vulkan-Hpp have a signed underlying type (all but CullMode, whose flags are
 * unsigned), and a signed bit-field holds one bit less than its width. A field that is too narrow
 * for a value turns it into a negative one (frontFace eClockwise, 1, in one bit is -1) that the
 * driver rejects. The fields hold the core values: topology up to ePatchList (10), polygonMode
 * eFill, eLine and ePoint, all four cull modes, both front faces. The values of extensions, such as
 * PolygonMode::eFillRectangleNV, do not fit and are not supported.
 */
struct RasterizationOptions
{
    // PipelineInputAssemblyStateCreateInfo
    PrimitiveTopology topology              : 5 = PrimitiveTopology::eTriangleList;
    // PipelineRasterizationStateCreateInfo
    bool        rasterizationDiscardEnabled : 1 = false;
    PolygonMode polygonMode                 : 3 = PolygonMode::eFill;
    CullMode    cullMode                    : 2 = CullMode::eBack;
    FrontFace   frontFace                   : 2 = FrontFace::eCounterClockwise;
    bool        primitiveRestartEnabled     : 1 = false;
};

using DepthStencilState = vk::PipelineDepthStencilStateCreateInfo;
using MultisampleState  = vk::PipelineMultisampleStateCreateInfo;
using ViewportState     = vk::PipelineViewportStateCreateInfo;
using DynamicStates     = StaticVector<vk::DynamicState, Limits::gMaxNumDynamicStates>;

class GraphicsPipeline
    : public DeviceChild
    , private vk::raii::Pipeline
{
public:
    using vk::raii::Pipeline::operator*;

    struct ColorBlendState
    {
        StaticVector<vk::PipelineColorBlendAttachmentState, Limits::gMaxNumAttachments> attachments;
    };

    /**
     * The pipeline has no layout (it reads its resources from the descriptor heap) and is built
     * with the device's pipeline cache, so neither is a parameter.
     */
    struct Parameters
    {
        ShaderProgramPtr     shading;
        VertexLayout         vertexLayout;
        RasterizationOptions rasterizer;
        ViewportState        viewportState;
        DepthStencilState    depthStencil;
        ColorBlendState      blending;
        MultisampleState     multisample;
        DynamicStates        dynamicStates;
        RenderingDescription renderingDescription;
    };

private:
    friend class Device;

    explicit GraphicsPipeline(DevicePtr device, Parameters createInfo);

public:
    const ShaderProgramPtr program;
};
} // namespace VOG::Graphics::Vulkan
