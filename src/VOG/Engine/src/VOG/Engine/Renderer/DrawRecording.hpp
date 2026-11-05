#pragma once

#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/PipelineTable.hpp"
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(HeapBuffer);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * A draw of a packet, by its index in the packet, and the pipeline it is drawn with, by its index
 * in the pipeline table. The handles stay in the packet: the render thread orders and records
 * these small values.
 */
struct ResolvedDraw
{
    std::uint32_t index;
    std::uint32_t pipelineIndex;
};

/**
 * What the render thread does with the draws of a packet, apart from the swapchain: the renderer
 * calls it for the image it acquired, and a test calls it for a render target it can read.
 */
struct DrawRecording
{
    /**
     * Finds the pipeline of every draw of @p packet in @p pipelines for a color target of
     * @p colorFormat, building the ones that are missing, and puts the draws in @p resolved, in the
     * order of the packet. @p resolved is cleared first; it is a parameter to keep its capacity.
     * No image is acquired yet when a pipeline fails to build.
     */
    static void resolve(const FramePacket&         packet,
                        PipelineTable&             pipelines,
                        vk::Format                 colorFormat,
                        std::vector<ResolvedDraw>& resolved);

    /**
     * Writes the Frame block of @p packet (its camera, its time, a color target of @p extent) into
     * @p block. No draw of an earlier frame may read @p block any more.
     */
    static void writeFrameBlock(Graphics::Vulkan::HeapBuffer& block,
                                const FramePacket&            packet,
                                vk::Extent2D                  extent);

    /**
     * Records the draws of @p packet into @p pass, which draws into a target of @p extent: the
     * viewport, the scissor, then for every draw of @p resolved, in that order, its pipeline, its
     * resources, its push data and its mesh.
     */
    static void recordDraws(Graphics::Vulkan::CommandBufferRecorder::RenderingPass& pass,
                            const FramePacket&                                      packet,
                            std::span<const ResolvedDraw>                           resolved,
                            const PipelineTable&                                    pipelines,
                            const Graphics::Vulkan::HeapBufferPtr&                  frameBlock,
                            vk::Extent2D                                            extent);
};
} // namespace VOG::Engine
