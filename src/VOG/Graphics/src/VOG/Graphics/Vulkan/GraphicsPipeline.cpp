#include "VOG/Graphics/Vulkan/GraphicsPipeline.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>

#include <stdexcept>

namespace VOG::Graphics::Vulkan
{
std::shared_ptr<GraphicsPipeline>
GraphicsPipeline::create(const Device&                               device,
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
                         std::uint32_t                               subpass)
{
    return std::make_shared<GraphicsPipeline>(device,
                                              cache,
                                              shading,
                                              vertexLayout,
                                              rasterizer,
                                              viewportState,
                                              depthStencil,
                                              blending,
                                              multisample,
                                              dynamicStates,
                                              pipelineLayout,
                                              renderPass,
                                              subpass);
}
GraphicsPipeline::GraphicsPipeline(const Device&                               device,
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
                                   std::uint32_t                               subpass)
    : vk::raii::Pipeline{nullptr}
{
    std::array<vk::PipelineShaderStageCreateInfo, 2> shadingStages = {
        vk::PipelineShaderStageCreateInfo{.stage  = vk::ShaderStageFlagBits::eVertex,
                                          .module = *shading.vertexFunction->module,
                                          .pName  = "main"},
        vk::PipelineShaderStageCreateInfo{.stage  = vk::ShaderStageFlagBits::eFragment,
                                          .module = *shading.fragmentFunction->module,
                                          .pName  = "main"}};

    const vk::PipelineVertexInputStateCreateInfo vertexInputState{
        .vertexBindingDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.bindingDescription.size()),
        .pVertexBindingDescriptions = vertexLayout.bindingDescription.data(),
        .vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(vertexLayout.attributeDescription.size()),
        .pVertexAttributeDescriptions = vertexLayout.attributeDescription.data()};
    const vk::PipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .topology               = rasterizer.topology,
        .primitiveRestartEnable = rasterizer.primitiveRestartEnabled};

    const vk::PipelineRasterizationStateCreateInfo rasterizationState{
        .depthClampEnable        = 0u,
        .rasterizerDiscardEnable = rasterizer.rasterizationDiscard,
        .polygonMode             = rasterizer.polygonMode,
        .cullMode                = rasterizer.cullMode,
        .frontFace               = rasterizer.frontFace,
        .depthBiasEnable         = 0u,
        .lineWidth               = 1.0f};
    const vk::PipelineDynamicStateCreateInfo dynamicState{
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates    = dynamicStates.data()};

    vk::GraphicsPipelineCreateInfo info{.pVertexInputState   = &vertexInputState,
                                        .pInputAssemblyState = &inputAssemblyState,
                                        .pViewportState      = &viewportState,
                                        .pRasterizationState = &rasterizationState,
                                        .pMultisampleState   = &multisample,
                                        .pDepthStencilState  = &depthStencil,
                                        .pColorBlendState    = &blending,
                                        .pDynamicState       = &dynamicState,
                                        .layout              = pipelineLayout,
                                        .renderPass          = renderPass,
                                        .subpass             = subpass};
    info.setStages(shadingStages);

    static_cast<vk::raii::Pipeline&>(*this) = vk::raii::Pipeline{device, cache, info};
}
} // namespace VOG::Graphics::Vulkan
