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
VOG_DECLARE_PTR(RenderTarget);
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * A draw of a packet, by its index in the packet, and the pipeline it is drawn with, by its index
 * in the pipeline table. The handles stay in the packet: the render thread orders and records
 * these small values. The sort key is made of integers that the renderer gave to the objects when
 * it made them, never of an address: of the material id and the mesh id for an opaque draw (after
 * the pipeline index), and of the distance from the camera, as an integer that grows as the draw
 * gets nearer, for a blended one.
 */
struct ResolvedDraw
{
    std::uint32_t index;
    std::uint32_t pipelineIndex;
    std::uint64_t sortKey;
};

static_assert(sizeof(ResolvedDraw) == 2u * sizeof(std::uint64_t));

/**
 * What the render thread does with the draws of a packet, apart from the swapchain: the renderer
 * calls it for the image it acquired, and a test calls it for a render target it can read.
 */
struct DrawRecording
{
    /**
     * Finds the pipeline of every draw of @p packet in @p pipelines for a color target of
     * @p colorFormat and a depth target of @p depthFormat, building the ones that are missing, and
     * puts the draws in @p resolved in the order they are recorded in. The opaque draws (blend
     * mode eOpaque) come first, sorted by pipeline, then material, then mesh, to bind as little as
     * possible; then the blended ones, from the farthest to the nearest along the view direction of
     * the camera of the packet, which the translation of the world matrix of a draw decides. Equal
     * keys keep the order of the packet. @p resolved is cleared first; it is a parameter to keep
     * its capacity. No image is acquired yet when a pipeline fails to build.
     */
    static void resolve(const FramePacket&         packet,
                        PipelineTable&             pipelines,
                        vk::Format                 colorFormat,
                        vk::Format                 depthFormat,
                        std::vector<ResolvedDraw>& resolved);

    /**
     * The sort key of a blended draw at @p viewDepth along the view direction: the keys of two
     * depths order in the opposite way, so the farthest draw has the smallest key. A depth of -0
     * and one of +0 have the same key.
     */
    static std::uint64_t blendedKey(float viewDepth);

    /** What the color target of @p packet is cleared to: the clear color of its camera. */
    static vk::ClearColorValue clearColor(const FramePacket& packet);

    /**
     * Records the barrier that makes the depth target ready for a pass, and returns the depth
     * attachment of that pass: cleared to 1 at the start, not kept at the end. One depth target
     * serves every frame in flight, so the barrier must make the depth writes of the frames before
     * visible to this one, with an access, and not only order them: its source scope is the
     * fragment test stages with the depth write access. The image is undefined before; it is
     * cleared, so nothing is lost.
     */
    static Graphics::Vulkan::CommandBufferRecorder::DepthAttachment
    prepareDepth(Graphics::Vulkan::CommandBufferRecorder& recorder,
                 const Graphics::Vulkan::RenderTargetPtr& depthTarget);

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
