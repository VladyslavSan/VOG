#include "VOG/Graphics/Vulkan/UploadBarriers.hpp"

#include <gtest/gtest.h>

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
// A device with a transfer-only family 1 beside its graphics family 0.
constexpr std::uint32_t gTransferFamily = 1u;
constexpr std::uint32_t gGraphicsFamily = 0u;

constexpr vk::PipelineStageFlags2 gTransferOnlyStages = vk::PipelineStageFlagBits2::eCopy;
} // namespace

/**
 * The two halves of an ownership transfer, for a buffer. This is the only check of the
 * cross-family barriers: no driver the tests run on has two families.
 */
TEST(UploadBarriers, crossFamilyBufferReleaseMatchesAcquire)
{
    const auto barriers = bufferUploadBarriers({}, gTransferFamily, gGraphicsFamily);
    ASSERT_TRUE(barriers.release);
    ASSERT_TRUE(barriers.acquire);
    const vk::BufferMemoryBarrier2& release = *barriers.release;
    const vk::BufferMemoryBarrier2& acquire = *barriers.acquire;

    // The same transfer in both halves, from the copying family to the using one.
    EXPECT_EQ(release.srcQueueFamilyIndex, gTransferFamily);
    EXPECT_EQ(release.dstQueueFamilyIndex, gGraphicsFamily);
    EXPECT_EQ(acquire.srcQueueFamilyIndex, release.srcQueueFamilyIndex);
    EXPECT_EQ(acquire.dstQueueFamilyIndex, release.dstQueueFamilyIndex);
    EXPECT_EQ(acquire.buffer, release.buffer);
    EXPECT_EQ(acquire.offset, release.offset);
    EXPECT_EQ(acquire.size, release.size);

    // The release runs on a queue that may support nothing but transfer: it names no stage past
    // the copy, and a destination of none. The acquire names no source, and reads the buffer as
    // vertices and indices.
    EXPECT_EQ(release.srcStageMask, vk::PipelineStageFlagBits2::eCopy);
    EXPECT_EQ(release.srcAccessMask, vk::AccessFlagBits2::eTransferWrite);
    EXPECT_FALSE(release.dstStageMask & ~gTransferOnlyStages);
    EXPECT_EQ(release.dstAccessMask, vk::AccessFlags2{});
    EXPECT_EQ(acquire.srcStageMask, vk::PipelineStageFlags2{});
    EXPECT_EQ(acquire.srcAccessMask, vk::AccessFlags2{});
    EXPECT_EQ(acquire.dstStageMask,
              vk::PipelineStageFlagBits2::eVertexAttributeInput |
                  vk::PipelineStageFlagBits2::eIndexInput);
    EXPECT_EQ(acquire.dstAccessMask,
              vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead);
}

/** As above, for an image: the layout transition is the same in both halves. */
TEST(UploadBarriers, crossFamilyTextureReleaseMatchesAcquire)
{
    const auto barriers = textureUploadBarriers({}, gTransferFamily, gGraphicsFamily);
    ASSERT_TRUE(barriers.acquire);
    const vk::ImageMemoryBarrier2& release = barriers.release;
    const vk::ImageMemoryBarrier2& acquire = *barriers.acquire;

    EXPECT_EQ(release.srcQueueFamilyIndex, gTransferFamily);
    EXPECT_EQ(release.dstQueueFamilyIndex, gGraphicsFamily);
    EXPECT_EQ(acquire.srcQueueFamilyIndex, release.srcQueueFamilyIndex);
    EXPECT_EQ(acquire.dstQueueFamilyIndex, release.dstQueueFamilyIndex);
    EXPECT_EQ(acquire.image, release.image);
    EXPECT_EQ(acquire.subresourceRange.aspectMask, release.subresourceRange.aspectMask);
    EXPECT_EQ(acquire.subresourceRange.levelCount, release.subresourceRange.levelCount);
    EXPECT_EQ(acquire.subresourceRange.layerCount, release.subresourceRange.layerCount);

    // The transition runs once, and both halves must name it the same.
    EXPECT_EQ(release.oldLayout, vk::ImageLayout::eTransferDstOptimal);
    EXPECT_EQ(release.newLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    EXPECT_EQ(acquire.oldLayout, release.oldLayout);
    EXPECT_EQ(acquire.newLayout, release.newLayout);

    EXPECT_EQ(release.srcStageMask, vk::PipelineStageFlagBits2::eCopy);
    EXPECT_EQ(release.srcAccessMask, vk::AccessFlagBits2::eTransferWrite);
    EXPECT_FALSE(release.dstStageMask & ~gTransferOnlyStages);
    EXPECT_EQ(release.dstAccessMask, vk::AccessFlags2{});
    EXPECT_EQ(acquire.srcStageMask, vk::PipelineStageFlags2{});
    EXPECT_EQ(acquire.srcAccessMask, vk::AccessFlags2{});
    EXPECT_EQ(acquire.dstStageMask,
              vk::PipelineStageFlagBits2::eVertexShader |
                  vk::PipelineStageFlagBits2::eFragmentShader);
    EXPECT_EQ(acquire.dstAccessMask, vk::AccessFlagBits2::eShaderSampledRead);
}

/**
 * With one family there is no ownership transfer, and no acquire: a pair would run the layout
 * transition twice, and the second one would name the wrong old layout. The image keeps one
 * barrier after the copy, which names the shader stages itself.
 */
TEST(UploadBarriers, sameFamilyHasNoAcquire)
{
    const auto buffer = bufferUploadBarriers({}, gGraphicsFamily, gGraphicsFamily);
    EXPECT_FALSE(buffer.release);
    EXPECT_FALSE(buffer.acquire);

    const auto texture = textureUploadBarriers({}, gGraphicsFamily, gGraphicsFamily);
    EXPECT_FALSE(texture.acquire);
    EXPECT_EQ(texture.release.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(texture.release.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(texture.release.oldLayout, vk::ImageLayout::eTransferDstOptimal);
    EXPECT_EQ(texture.release.newLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
    EXPECT_EQ(texture.release.dstStageMask,
              vk::PipelineStageFlagBits2::eVertexShader |
                  vk::PipelineStageFlagBits2::eFragmentShader);
    EXPECT_EQ(texture.release.dstAccessMask, vk::AccessFlagBits2::eShaderSampledRead);
}
} // namespace VOG::Tests
