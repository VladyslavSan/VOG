#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <boost/container/small_vector.hpp>

namespace VOG::Graphics::Vulkan
{
class Device;

struct VertexLayout
{
    boost::container::small_vector<vk::VertexInputBindingDescription, 2>   bindingDescription;
    boost::container::small_vector<vk::VertexInputAttributeDescription, 8> attributeDescription;
};

struct RasterizationOptions
{
    // PipelineInputAssemblyStateCreateInfo
    vk::PrimitiveTopology topology            : 4 = vk::PrimitiveTopology::eTriangleList;
    // PipelineRasterizationStateCreateInfo
    bool                 rasterizationDiscard : 1 = false;
    vk::PolygonMode      polygonMode          : 2 = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode             : 2 = vk::CullModeFlagBits::eBack;
    vk::FrontFace        frontFace            : 1 = vk::FrontFace::eCounterClockwise;
};

class GraphicsPipeline : public vk::raii::Pipeline
{
public:
    using DepthStencilState = vk::PipelineDepthStencilStateCreateInfo;
    using ColorBlendState   = vk::PipelineColorBlendStateCreateInfo;
    using MultisampleState  = vk::PipelineMultisampleStateCreateInfo;
    using ViewportState     = vk::PipelineViewportStateCreateInfo;
    using DynamicStates     = boost::container::small_vector<vk::DynamicState, 4>;

    static std::shared_ptr<GraphicsPipeline>
    create(const Device&                               device,
           vk::Optional<const vk::raii::PipelineCache> cache,
           const ShadingStages&                        shading,
           const VertexLayout&                         vertexLayout,
           const RasterizationOptions&                 rasterizer,
           const ViewportState&                        viewportState,
           const DepthStencilState&                    depthStencil,
           const ColorBlendState&                      blending,
           const MultisampleState&                     multisample,
           const DynamicStates&                        dynamicStates,
           vk::PipelineLayout                          pipelineLayout,
           vk::RenderPass                              renderPass,
           std::uint32_t                               subpass);

    GraphicsPipeline(const Device&                               device,
                     vk::Optional<const vk::raii::PipelineCache> cache,
                     const ShadingStages&                        shading,
                     const VertexLayout&                         vertexLayout,
                     const RasterizationOptions&                 rasterizer,
                     const ViewportState&                        viewportState,
                     const DepthStencilState&                    depthStencil,
                     const ColorBlendState&                      blending,
                     const MultisampleState&                     multisample,
                     const DynamicStates&                        dynamicStates,
                     vk::PipelineLayout                          pipelineLayout,
                     vk::RenderPass                              renderPass,
                     std::uint32_t                               subpass);
};
} // namespace VOG::Graphics::Vulkan
