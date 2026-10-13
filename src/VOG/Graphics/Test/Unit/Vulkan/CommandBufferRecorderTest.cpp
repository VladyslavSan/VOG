#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>
#include <type_traits>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
// The raw vk::raii::CommandBuffer base is private: recording must go through the recorder.
static_assert(!std::is_convertible_v<CommandBuffer*, vk::raii::CommandBuffer*>);

namespace
{
constexpr vk::DeviceSize gBufferSize = 256u;

constexpr std::uint64_t gWaitForever = std::numeric_limits<std::uint64_t>::max();

constexpr std::array<vk::Semaphore, 0>          gNoSemaphores{};
constexpr std::array<vk::PipelineStageFlags, 0> gNoStages{};
} // namespace

/**
 * A resource passed to a barrier is retained by the command buffer: it outlives the caller's last
 * reference and dies only when the pool resets, after the fence has signaled.
 */
TEST_F(VulkanFixture, CommandBufferRecorder_barrierRetainsBufferUntilPoolReset)
{
    std::shared_ptr<Buffer> buffer = VulkanDevice->createBuffer(
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.usage = VMA_MEMORY_USAGE_AUTO});
    const std::weak_ptr<Buffer> weakBuffer = buffer;

    auto fencePool     = FencePool::create(VulkanDevice);
    auto pool          = VulkanDevice->createCommandBufferPool();
    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);

    {
        CommandBufferRecorder recorder{**commandBuffer};
        recorder.setBufferBarrier(buffer,
                                  {.srcStageMask        = vk::PipelineStageFlagBits2::eTransfer,
                                   .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                                   .dstStageMask        = vk::PipelineStageFlagBits2::eVertexInput,
                                   .dstAccessMask       = vk::AccessFlagBits2::eVertexAttributeRead,
                                   .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                   .offset              = 0u,
                                   .size                = VK_WHOLE_SIZE});
        recorder.finish();
    }

    buffer.reset();
    EXPECT_FALSE(weakBuffer.expired());

    const auto submission = VulkanDevice->getGraphicsQueue()->submit(
        fencePool, gNoSemaphores, gNoStages, std::array{std::move(commandBuffer)}, gNoSemaphores);
    ASSERT_EQ(submission.fence->wait(gWaitForever), vk::Result::eSuccess);
    EXPECT_FALSE(weakBuffer.expired());

    pool->reset();
    EXPECT_TRUE(weakBuffer.expired());
}

/**
 * The whole frame path without a swapchain: barrier, cleared rendering pass, submit, fence wait.
 * Runs under the validation layer: a driver accepts a null view or a missing vkCmdEndRendering
 * silently, so only the layer can fail this test.
 */
TEST_F(VulkanValidationFixture, CommandBufferRecorder_clearRenderBufferAndSubmit)
{
    if (!validationActive())
        GTEST_SKIP() << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils not available";

    std::shared_ptr<RenderBuffer> target =
        VulkanDevice->createRenderBuffer(AttachmentUsage::eColor,
                                         vk::Format::eR8G8B8A8Unorm,
                                         {.width = 64u, .height = 64u},
                                         SampleCount::e1,
                                         1u,
                                         1u,
                                         vk::ImageTiling::eOptimal,
                                         vk::ImageLayout::eUndefined,
                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

    auto fencePool     = FencePool::create(VulkanDevice);
    auto pool          = VulkanDevice->createCommandBufferPool();
    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);

    {
        CommandBufferRecorder recorder{**commandBuffer};
        recorder.setImageBarrier(target, toColorAttachmentBarrier(target->getImage()));

        {
            auto pass = recorder.beginRendering(
                {{.attachment = target,
                  .loadOp     = vk::AttachmentLoadOp::eClear,
                  .storeOp    = vk::AttachmentStoreOp::eStore,
                  .clearValue = vk::ClearColorValue{std::array{0.25f, 0.5f, 0.75f, 1.0f}}}});
        }

        recorder.finish();
    }

    const auto submission = VulkanDevice->getGraphicsQueue()->submit(
        fencePool, gNoSemaphores, gNoStages, std::array{std::move(commandBuffer)}, gNoSemaphores);

    EXPECT_EQ(submission.fence->wait(gWaitForever), vk::Result::eSuccess);

    pool->reset();

    EXPECT_TRUE(validationErrors.empty())
        << (validationErrors.empty() ? "" : validationErrors.front());
}
} // namespace VOG::Tests
