#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
constexpr std::uint32_t gKiB = 1024u;

/** Limits of a GPU that is stricter than lavapipe: big heap alignment, reserved ranges. */
vk::PhysicalDeviceDescriptorHeapPropertiesEXT
strictLimits()
{
    vk::PhysicalDeviceDescriptorHeapPropertiesEXT limits{};
    limits.samplerHeapAlignment         = 65536u;
    limits.resourceHeapAlignment        = 65536u;
    limits.maxSamplerHeapSize           = 1024u * gKiB * gKiB;
    limits.maxResourceHeapSize          = 1024u * gKiB * gKiB;
    limits.minSamplerHeapReservedRange  = 96u * 32u;
    limits.minResourceHeapReservedRange = 2u * gKiB * gKiB;
    limits.samplerDescriptorSize        = 32u;
    limits.imageDescriptorSize          = 64u;
    limits.bufferDescriptorSize         = 128u;
    limits.samplerDescriptorAlignment   = 32u;
    limits.imageDescriptorAlignment     = 64u;
    limits.bufferDescriptorAlignment    = 128u;

    return limits;
}

constexpr std::array<std::byte, 16> gTint{};
} // namespace

/**
 * Slots sit 128 bytes apart, the reserved range follows the last slot, and every size and offset
 * covers what the limits ask for.
 */
TEST(DescriptorHeapLayout, placesSlotsAndReservedRangesForRealGpuLimits)
{
    const auto limits = strictLimits();
    const auto layout = DescriptorHeapLayout::make(limits, 100u, 3u);

    for (std::uint32_t i = 0u; i < 4u; ++i)
    {
        EXPECT_EQ(DescriptorHeapLayout::slotOffset(i), i * 128u);
    }

    EXPECT_EQ(layout.resources.reservedRangeOffset, 100u * 128u);
    EXPECT_EQ(layout.resources.reservedRangeSize, limits.minResourceHeapReservedRange);
    EXPECT_GE(layout.resources.size,
              layout.resources.reservedRangeOffset + layout.resources.reservedRangeSize);
    EXPECT_EQ(layout.resources.alignment, limits.resourceHeapAlignment);

    EXPECT_EQ(layout.samplerStride, limits.samplerDescriptorSize);
    EXPECT_GE(layout.samplers.reservedRangeOffset, 3u * limits.samplerDescriptorSize);
    EXPECT_EQ(layout.samplers.reservedRangeOffset % limits.samplerDescriptorAlignment, 0u);
    EXPECT_EQ(layout.samplers.reservedRangeSize, limits.minSamplerHeapReservedRange);
    EXPECT_GE(layout.samplers.size,
              layout.samplers.reservedRangeOffset + layout.samplers.reservedRangeSize);
    EXPECT_EQ(layout.samplers.alignment, limits.samplerHeapAlignment);
}

TEST(DescriptorHeapLayout, rejectsCapacityBeyondMaxHeapSize)
{
    auto limits = strictLimits();

    // The resource heap fits exactly with 8192 slots (1 MiB of slots + 2 MiB reserved).
    limits.maxResourceHeapSize = 3u * gKiB * gKiB;
    EXPECT_NO_THROW(DescriptorHeapLayout::make(limits, 8192u, 1u));
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 8193u, 1u), std::runtime_error);

    // The sampler heap fits exactly with 96 samplers (reserved range of 96 * 32 bytes after them).
    limits.maxSamplerHeapSize = 2u * 96u * 32u;
    EXPECT_NO_THROW(DescriptorHeapLayout::make(limits, 1u, 96u));
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 97u), std::runtime_error);
}

/**
 * A descriptor that is bigger than a slot, or whose alignment a slot start breaks, is refused, and
 * so is a heap with no slot or no sampler.
 */
TEST(DescriptorHeapLayout, rejectsDescriptorsThatDoNotFitTheSlot)
{
    auto limits = strictLimits();
    EXPECT_NO_THROW(DescriptorHeapLayout::make(limits, 1u, 1u));

    limits                          = strictLimits();
    limits.imageDescriptorAlignment = 256u;
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 1u), std::runtime_error);

    limits                     = strictLimits();
    limits.imageDescriptorSize = 256u;
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 1u), std::runtime_error);

    limits                      = strictLimits();
    limits.bufferDescriptorSize = 256u;
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 1u), std::runtime_error);

    limits                           = strictLimits();
    limits.bufferDescriptorAlignment = 256u;
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 1u), std::runtime_error);

    limits                            = strictLimits();
    limits.samplerDescriptorAlignment = 64u;
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 1u), std::runtime_error);

    limits = strictLimits();
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 0u, 1u), std::invalid_argument);
    EXPECT_THROW(DescriptorHeapLayout::make(limits, 1u, 0u), std::invalid_argument);
}

/**
 * A full heap throws, and the slots that are taken stay valid: they keep their own indices, and
 * the failed allocation took none.
 */
TEST_F(VulkanFixture, DescriptorHeap_fullHeapThrows)
{
    auto heap = createHeap(2u);

    auto first  = mDevice->createUniformBlock(heap, gTint);
    auto second = mDevice->createUniformBlock(heap, gTint);
    EXPECT_NE(first->heapIndex(), second->heapIndex());
    EXPECT_LT(first->heapIndex(), 2u);
    EXPECT_LT(second->heapIndex(), 2u);

    EXPECT_THROW(mDevice->createUniformBlock(heap, gTint), std::runtime_error);
    EXPECT_THROW(
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb),
        std::runtime_error);

    EXPECT_NE(first->heapIndex(), second->heapIndex());
    first.reset();
    EXPECT_NO_THROW(mDevice->createUniformBlock(heap, gTint));
}

/**
 * A slot is handed out again only after the command buffer that recorded its resource has been
 * reset: dropping the resource while that command buffer is in flight frees nothing.
 */
TEST_F(VulkanValidationFixture, DescriptorHeap_slotReusedOnlyAfterRetainingPoolReset)
{
    auto                          heap = createHeap(1u);
    std::shared_ptr<RenderBuffer> target =
        mDevice->createRenderBuffer(AttachmentUsage::eColor,
                                    vk::Format::eR8G8B8A8Unorm,
                                    {.width = 16u, .height = 16u},
                                    SampleCount::e1,
                                    1u,
                                    1u,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageLayout::eUndefined,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);
    std::shared_ptr<UniformBlock> block = mDevice->createUniformBlock(heap, gTint);

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    auto recorder  = pool->record();
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering({{.attachment = target}});
        pass.useResource(block);
    }
    auto executable = recorder.finish();

    block.reset();
    EXPECT_THROW(mDevice->createUniformBlock(heap, gTint), std::runtime_error);

    mDevice->getGraphicsQueue()->submit(*fencePool, {}, std::span{&executable, 1u}, {});
    EXPECT_THROW(mDevice->createUniformBlock(heap, gTint), std::runtime_error);

    pool->reset();
    EXPECT_NO_THROW(mDevice->createUniformBlock(heap, gTint));
    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * The first proof that the validation layer understands descriptor heaps: a draw that samples a
 * texture and reads a uniform block by heap index, with the texture uploaded in the same command
 * buffer. Any error about the heap buffers (usage, alignment, device address), the descriptor
 * writes (size), the push data, the pipeline without a layout or the upload fails it.
 */
TEST_F(VulkanValidationFixture, DescriptorHeap_texturedDrawIsValidationClean)
{
    Graphics::ShaderProgramCache cache{mDevice, VOG_TEST_SHADER_PATH};
    auto                         heap = createHeap();

    // Four texels of 4 bytes, and a tint of 4 floats.
    const std::array<std::uint8_t, 16> pixels{
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const std::array<float, 4> tint{1.0f, 1.0f, 1.0f, 1.0f};

    BufferPtr                staging = createStaging(std::as_bytes(std::span{pixels}));
    std::shared_ptr<Texture> texture =
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb);
    std::shared_ptr<UniformBlock> block =
        mDevice->createUniformBlock(heap, std::as_bytes(std::span{tint}));

    // Position, then uv.
    const std::array<float, 12> vertices{
        -1.0f, -1.0f, 0.0f, 0.0f, 3.0f, -1.0f, 2.0f, 0.0f, -1.0f, 3.0f, 0.0f, 2.0f};
    BufferPtr vertexBuffer = mDevice->createBuffer(
        {.size = sizeof(vertices), .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.flags = AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = vertexBuffer->mapForWrite();
        std::memcpy(mapping.data, vertices.data(), sizeof(vertices));
    }

    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;
    GraphicsPipelinePtr pipeline = mDevice->createGraphicsPipeline(
        {.shading       = cache.get("TestDescriptorHeap"),
         .vertexLayout  = {.bindingDescription   = {{.binding   = 0u,
                                                     .stride    = 16u,
                                                     .inputRate = vk::VertexInputRate::eVertex}},
                           .attributeDescription = {{.location = 0u,
                                                     .binding  = 0u,
                                                     .format   = vk::Format::eR32G32Sfloat,
                                                     .offset   = 0u},
                                                    {.location = 1u,
                                                     .binding  = 0u,
                                                     .format   = vk::Format::eR32G32Sfloat,
                                                     .offset   = 8u}}},
         .rasterizer    = {.cullMode = CullMode::eNone},
         .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
         .depthStencil  = {},
         .blending      = {.attachments = {{.colorWriteMask = colorWriteMask}}},
         .multisample   = {},
         .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
         .renderingDescription = {.colorAttachmentFormats = {vk::Format::eR8G8B8A8Unorm}}});

    std::shared_ptr<RenderBuffer> target =
        mDevice->createRenderBuffer(AttachmentUsage::eColor,
                                    vk::Format::eR8G8B8A8Unorm,
                                    {.width = 64u, .height = 64u},
                                    SampleCount::e1,
                                    1u,
                                    1u,
                                    vk::ImageTiling::eOptimal,
                                    vk::ImageLayout::eUndefined,
                                    vk::MemoryPropertyFlagBits::eDeviceLocal);

    // The push block of the shader: mat4 mvp, uint material, uint textures[4].
    struct PushData
    {
        std::array<float, 16>        mvp;
        std::uint32_t                material;
        std::array<std::uint32_t, 4> textures;
    };

    const PushData pushData{.mvp      = {1.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         1.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         1.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         0.0f,
                                         1.0f},
                            .material = block->heapIndex(),
                            .textures = {texture->heapIndex(), 0u, 0u, 0u}};

    auto fencePool = FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    auto recorder  = pool->record();
    recorder.bindDescriptorHeap(heap);
    recorder.copyBufferToTexture(staging, texture, mDevice->queueInfos.graphics.familyIndex);
    recorder.setImageBarrier(target, VulkanFixture::toColorAttachmentBarrier());
    {
        auto pass = recorder.beginRendering({{.attachment = target}});
        pass.setViewport(0, {{.width = 64.0f, .height = 64.0f, .maxDepth = 1.0f}});
        pass.setScissor(0, {{.extent = {.width = 64u, .height = 64u}}});
        pass.bindPipeline(pipeline);
        pass.bindVertexBuffers(0u, {{.buffer = vertexBuffer, .offset = 0u}});
        pass.useResource(texture);
        pass.useResource(block);
        pass.pushData(0u, std::as_bytes(std::span{&pushData, 1u}));
        pass.draw(3u, 1u, 0u, 0u);
    }
    auto executable = recorder.finish();
    mDevice->getGraphicsQueue()->submit(*fencePool, {}, std::span{&executable, 1u}, {});

    // Waits the fence of the submit.
    pool->reset();

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}
} // namespace VOG::Tests
