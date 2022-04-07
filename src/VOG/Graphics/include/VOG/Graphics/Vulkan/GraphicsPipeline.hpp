#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

namespace VOG::Graphics::Vulkan
{
constexpr std::uint8_t gMaxNumVertexBuffers    = 4u;
constexpr std::uint8_t gMaxNumVertexAttributes = 8u;
class Device;

struct VertexLayout
{
    template <class T, std::size_t N>
    using Container = boost::container::static_vector<T, N>;

    Container<vk::VertexInputBindingDescription, gMaxNumVertexBuffers>      bindingDescription;
    Container<vk::VertexInputAttributeDescription, gMaxNumVertexAttributes> attributeDescription;
};

struct RasterizationOptions
{
    // PipelineInputAssemblyStateCreateInfo
    vk::PrimitiveTopology topology               : 4 = vk::PrimitiveTopology::eTriangleList;
    // PipelineRasterizationStateCreateInfo
    vk::Bool32           rasterizationDiscard    : 1 = 0u;
    vk::PolygonMode      polygonMode             : 2 = vk::PolygonMode::eFill;
    vk::CullModeFlagBits cullMode                : 2 = vk::CullModeFlagBits::eBack;
    vk::FrontFace        frontFace               : 1 = vk::FrontFace::eCounterClockwise;
    vk::Bool32           primitiveRestartEnabled : 1 = 0u;
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
           const ShaderProgram&                        shading,
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
                     const ShaderProgram&                        shading,
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
