#pragma once

#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>

#include <span>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(GraphicsPipeline);
VOG_DECLARE_PTR(HeapBuffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * What the render thread does with the draws of a packet, apart from the swapchain: the renderer
 * calls it for the image it acquired, and a test calls it for a render target it can read.
 */
struct DrawRecording
{
    /**
     * Writes the Frame block of @p packet (its camera, its time, a color target of @p extent) into
     * @p block. No draw of an earlier frame may read @p block any more.
     */
    static void writeFrameBlock(Graphics::Vulkan::HeapBuffer& block,
                                const FramePacket&            packet,
                                vk::Extent2D                  extent);

    /**
     * Records the draws of @p packet into @p pass, which draws into a target of @p extent: the
     * viewport, the scissor, then for every draw its pipeline, its resources, its push data and
     * its mesh. @p pipelines[i] is the pipeline of draw i.
     */
    static void recordDraws(Graphics::Vulkan::CommandBufferRecorder::RenderingPass& pass,
                            const FramePacket&                                      packet,
                            std::span<const Graphics::Vulkan::GraphicsPipelinePtr>  pipelines,
                            const Graphics::Vulkan::HeapBufferPtr&                  frameBlock,
                            vk::Extent2D                                            extent);
};
} // namespace VOG::Engine
