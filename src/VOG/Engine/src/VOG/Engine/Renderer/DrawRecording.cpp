#include "VOG/Engine/Renderer/DrawRecording.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <tuple>

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

namespace
{
/** The bits of an id: the material id is the high half of the sort key of an opaque draw. */
constexpr unsigned gIdBits = 32u;

/** An unsigned integer that orders like @p value does as a float, for a value that is not NaN. */
std::uint32_t
orderedBits(float value)
{
    const auto bits = std::bit_cast<std::uint32_t>(value);

    constexpr std::uint32_t kSignBit = 0x80000000u;

    return (bits & kSignBit) != 0u ? ~bits : bits | kSignBit;
}
} // namespace

std::uint64_t
DrawRecording::blendedKey(float viewDepth)
{
    // -0 and +0 are one distance: they must not order apart.
    if (viewDepth == 0.0f)
    {
        viewDepth = 0.0f;
    }

    return ~orderedBits(viewDepth);
}

void
DrawRecording::resolve(const FramePacket&         packet,
                       PipelineTable&             pipelines,
                       vk::Format                 colorFormat,
                       vk::Format                 depthFormat,
                       std::vector<ResolvedDraw>& resolved)
{
    resolved.clear();

    const auto isBlended = [](const auto& draw)
    {
        return draw.material.mData->state.blend != BlendMode::eOpaque;
    };
    const auto pipelineOf = [&](const auto& draw)
    {
        return pipelines.resolve(*draw.mesh.mData, *draw.material.mData, colorFormat, depthFormat);
    };

    // The opaque bucket, then the blended one: each is filled before it is sorted.
    for (const auto& [index, draw] : std::views::zip(std::views::iota(0u), packet.draws))
    {
        if (!isBlended(draw))
        {
            resolved.push_back(
                {.index         = index,
                 .pipelineIndex = pipelineOf(draw),
                 .sortKey       = (std::uint64_t{draw.material.mData->materialId} << gIdBits) |
                                  draw.mesh.mData->meshId});
        }
    }
    const auto opaqueEnd = static_cast<std::ptrdiff_t>(resolved.size());

    const Math::Matrix4x4f view = packet.camera.view();
    for (const auto& [index, draw] : std::views::zip(std::views::iota(0u), packet.draws))
    {
        if (isBlended(draw))
        {
            // Along the view direction +Z, a larger z is farther; the key grows as z falls.
            const float depth = (view * draw.world[3]).z;
            resolved.push_back(
                {.index = index, .pipelineIndex = pipelineOf(draw), .sortKey = blendedKey(depth)});
        }
    }

    std::ranges::stable_sort(std::span{resolved}.first(static_cast<std::size_t>(opaqueEnd)),
                             [](const ResolvedDraw& left, const ResolvedDraw& right)
                             {
                                 return std::tie(left.pipelineIndex, left.sortKey) <
                                        std::tie(right.pipelineIndex, right.sortKey);
                             });
    std::ranges::stable_sort(std::span{resolved}.subspan(static_cast<std::size_t>(opaqueEnd)),
                             {},
                             &ResolvedDraw::sortKey);
}

vk::ClearColorValue
DrawRecording::clearColor(const FramePacket& packet)
{
    const Math::Vector4f& clear = packet.camera.clearColor;

    return vk::ClearColorValue{std::array{clear.r, clear.g, clear.b, clear.a}};
}

Graphics::Vulkan::CommandBufferRecorder::DepthAttachment
DrawRecording::prepareDepth(Graphics::Vulkan::CommandBufferRecorder& recorder,
                            const Graphics::Vulkan::RenderTargetPtr& depthTarget)
{
    constexpr auto kDepthTests = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                 vk::PipelineStageFlagBits2::eLateFragmentTests;

    recorder.setImageBarrier(depthTarget,
                             {.srcStageMask  = kDepthTests,
                              .srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                              .dstStageMask  = kDepthTests,
                              .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                                               vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                              .oldLayout     = vk::ImageLayout::eUndefined,
                              .newLayout     = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = {.aspectMask   = vk::ImageAspectFlagBits::eDepth,
                                                      .baseMipLevel = 0u,
                                                      .levelCount   = VK_REMAINING_MIP_LEVELS,
                                                      .baseArrayLayer = 0u,
                                                      .layerCount     = VK_REMAINING_ARRAY_LAYERS}});

    return {.attachment = depthTarget,
            .loadOp     = vk::AttachmentLoadOp::eClear,
            .storeOp    = vk::AttachmentStoreOp::eDontCare,
            .clearValue = {.depth = 1.0f, .stencil = 0u}};
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
        .projection     = camera.projectionMatrix(viewport),
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
