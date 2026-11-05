#include "VOG/Engine/Renderer/DrawRecording.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>

namespace VOG::Engine
{
namespace
{
/**
 * Records the pipeline and buffer binds of one rendering pass, and leaves out a bind that would
 * repeat the previous bind of its kind in the pass. The pass is one command buffer, and the earlier
 * bind already retained the resource in it until the fence signals, so a skipped bind loses no
 * retention. That also makes comparing addresses sound: this command buffer retains every bound
 * object, so no new allocation can reuse an address while the pass records. A non-indexed draw
 * binds no index buffer and leaves the bound one as it was.
 */
class PassBinder
{
public:
    explicit PassBinder(Graphics::Vulkan::CommandBufferRecorder::RenderingPass& pass)
        : mPass{pass}
    {
    }

    void
    pipeline(const Graphics::Vulkan::GraphicsPipelinePtr& pipeline)
    {
        if (pipeline.get() != mPipeline)
        {
            mPass.bindPipeline(pipeline);
            mPipeline = pipeline.get();
        }
    }

    void
    vertexBuffer(const Graphics::Vulkan::BufferPtr& buffer)
    {
        if (buffer.get() != mVertexBuffer)
        {
            mPass.bindVertexBuffers(0u, {{.buffer = buffer, .offset = 0u}});
            mVertexBuffer = buffer.get();
        }
    }

    void
    indexBuffer(const Graphics::Vulkan::BufferPtr& buffer, vk::DeviceSize offset)
    {
        // Untested: every mesh owns its buffer, so the same buffer is always at the same offset;
        // meshes that share a buffer would reach the offset comparison.
        if (buffer.get() != mIndexBuffer || offset != mIndexOffset)
        {
            mPass.bindIndexBuffer(buffer, offset, vk::IndexType::eUint32);
            mIndexBuffer = buffer.get();
            mIndexOffset = offset;
        }
    }

private:
    Graphics::Vulkan::CommandBufferRecorder::RenderingPass& mPass;

    const Graphics::Vulkan::GraphicsPipeline* mPipeline     = nullptr;
    const Graphics::Vulkan::Buffer*           mVertexBuffer = nullptr;
    const Graphics::Vulkan::Buffer*           mIndexBuffer  = nullptr;
    vk::DeviceSize                            mIndexOffset  = 0u;
};
} // namespace

void
DrawRecording::resolve(const FramePacket&         packet,
                       PipelineTable&             pipelines,
                       vk::Format                 colorFormat,
                       std::vector<ResolvedDraw>& resolved)
{
    resolved.clear();
    for (const auto& [index, draw] : std::views::zip(std::views::iota(0u), packet.draws))
    {
        resolved.push_back({.index         = index,
                            .pipelineIndex = pipelines.resolve(
                                *draw.mesh.mData, *draw.material.mData, colorFormat)});
    }
}

void
DrawRecording::writeFrameBlock(Graphics::Vulkan::HeapBuffer& block,
                               const FramePacket&            packet,
                               vk::Extent2D                  extent)
{
    const CameraView& camera = packet.camera;
    const Extent      viewport{.width = extent.width, .height = extent.height};
    const FrameBlock  frameBlock{
        .view           = camera.view(),
        .projection     = camera.projection(viewport),
        .viewProjection = camera.viewProjection(viewport),
        .cameraPosition = {camera.position, 1.0f},
        .viewport       = {static_cast<float>(extent.width), static_cast<float>(extent.height)},
        .timeSeconds    = static_cast<float>(packet.frame.timeSeconds),
        .deltaSeconds   = packet.frame.deltaSeconds};

    auto mapping = block.mapForWrite();
    std::ranges::copy(std::as_bytes(std::span{&frameBlock, 1u}), mapping.bytes().begin());
}

void
DrawRecording::recordDraws(Graphics::Vulkan::CommandBufferRecorder::RenderingPass& pass,
                           const FramePacket&                                      packet,
                           std::span<const ResolvedDraw>                           resolved,
                           const PipelineTable&                                    pipelines,
                           const Graphics::Vulkan::HeapBufferPtr&                  frameBlock,
                           vk::Extent2D                                            extent)
{
    pass.setViewport(0,
                     {
                         {
                             .x        = 0.0f,
                             .y        = 0.0f,
                             .width    = static_cast<float>(extent.width),
                             .height   = static_cast<float>(extent.height),
                             .minDepth = 0.0,
                             .maxDepth = 1.0,
                         },
                     });
    pass.setScissor(0,
                    {
                        {
                            .offset = {.x = 0, .y = 0},
                            .extent = {.width = extent.width, .height = extent.height},
                        },
                    });

    PassBinder binder{pass};
    pass.useResource(frameBlock);

    for (const ResolvedDraw& resolvedDraw : resolved)
    {
        const RenderQueue::Draw&    draw     = packet.draws[resolvedDraw.index];
        const Detail::MeshData&     mesh     = *draw.mesh.mData;
        const Detail::MaterialData& material = *draw.material.mData;

        const DrawPushData pushData{.model    = draw.world,
                                    .material = material.block->heapIndex(),
                                    .frame    = frameBlock->heapIndex()};

        binder.pipeline(pipelines.at(resolvedDraw.pipelineIndex));
        pass.useResource(material.block);
        for (const Texture& texture : material.textures)
        {
            pass.useResource(texture.mData->texture);
        }
        pass.pushData(0u, std::as_bytes(std::span{&pushData, 1u}));
        binder.vertexBuffer(mesh.buffer);
        if (mesh.indexCount == 0u)
        {
            pass.draw(mesh.vertexCount, 1, 0, 0);
        }
        else
        {
            binder.indexBuffer(mesh.buffer, mesh.indexOffset);
            pass.drawIndexed(mesh.indexCount, 1, 0, 0, 0);
        }
    }
}
} // namespace VOG::Engine
