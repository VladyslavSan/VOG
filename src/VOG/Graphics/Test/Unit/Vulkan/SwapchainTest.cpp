#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include "VOG/Graphics/Frame/FrameObjectManager.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"
#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>

#include "VulkanHeadlessFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
/** Takes a freshly acquired image to the layout vkQueuePresentKHR expects. */
vk::ImageMemoryBarrier2
toPresentBarrier(vk::Image image)
{
    return {.srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask        = vk::PipelineStageFlagBits2::eBottomOfPipe,
            .oldLayout           = vk::ImageLayout::eUndefined,
            .newLayout           = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = image,
            .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                    .baseMipLevel   = 0u,
                                    .levelCount     = VK_REMAINING_MIP_LEVELS,
                                    .baseArrayLayer = 0u,
                                    .layerCount     = VK_REMAINING_ARRAY_LAYERS}};
}

/**
 * Has the GPU wait @p imageAvailable, so the acquire that signals it is done. An image that is
 * acquired and then never rendered to would otherwise leave a signal pending, and destroying the
 * semaphore (or the device) under a pending signal is invalid.
 */
void
consumeAcquireSignal(const DevicePtr& device, const SemaphorePtr& imageAvailable)
{
    auto fencePool     = FencePool::create(device);
    auto pool          = device->createCommandBufferPool();
    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
    {
        CommandBufferRecorder recorder{**commandBuffer};
        recorder.finish();
    }

    const std::array<SemaphorePtr, 0> noSignals{};
    const auto                        submission = device->getGraphicsQueue()->submit(
        fencePool,
        std::array{imageAvailable},
        std::array{vk::PipelineStageFlags{vk::PipelineStageFlagBits::eTopOfPipe}},
        std::array{std::move(commandBuffer)},
        noSignals);
    EXPECT_EQ(submission.fence->wait(std::numeric_limits<std::uint64_t>::max()),
              vk::Result::eSuccess);
    pool->reset();
}

/**
 * The renderer's frame, without the drawing: the same objects in the same declaration order, so
 * destroying a Rig is the renderer's shutdown.
 */
struct Rig
{
    std::shared_ptr<const Queue>                         queue;
    FencePoolPtr                                         fencePool;
    std::unique_ptr<Swapchain>                           swapchain;
    std::unique_ptr<Graphics::Frame::FrameObjectManager> frames;

    /**
     * Acquire, record a barrier, submit, present, hand the presentation to the frame slot.
     *
     * @param recordAcquired  Record the acquired image through setImageBarrier, so the command
     *                        buffer retains it. When false the barrier is raw, and only the
     *                        presentation keeps the swapchain alive.
     * @return the generation the frame was presented to.
     */
    SwapchainGenerationWeakPtr
    renderFrame(bool recordAcquired)
    {
        auto acquireResult = swapchain->acquireNextImage();
        EXPECT_EQ(acquireResult.status, Swapchain::AcquireStatus::eReady);
        const AcquiredSwapchainImagePtr acquired = acquireResult.acquired;
        if (!acquired)
        {
            return {};
        }

        auto frame = frames->acquireNextFrame();
        auto commandBuffer =
            frame->getCommandBufferPoolForThread(0u)->get(vk::CommandBufferLevel::ePrimary);
        {
            CommandBufferRecorder recorder{**commandBuffer};
            const auto            barrier = toPresentBarrier(acquired->getImage());
            if (recordAcquired)
            {
                recorder.setImageBarrier(acquired, barrier);
            }
            else
            {
                recorder.unsafeSetBarriers({}, {}, {barrier});
            }
            recorder.finish();
        }

        queue->submit(
            fencePool,
            std::array{acquireResult.imageAvailableSemaphore},
            std::array{vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput}},
            std::array{std::move(commandBuffer)},
            std::array{frame->getRenderFinishedSemaphore()});

        auto presented =
            swapchain->present(acquired, frame->getRenderFinishedSemaphore(), fencePool);
        EXPECT_EQ(presented.status, Swapchain::PresentStatus::eReady);

        SwapchainGenerationWeakPtr generation = presented.presentation.generation;
        frame->retainUntilPresented(std::move(presented.presentation));
        return generation;
    }
};
} // namespace

class SwapchainTest : public VulkanHeadlessFixture
{
protected:
    Rig
    makeRig()
    {
        return {.queue     = VulkanDevice->getGraphicsQueue(),
                .fencePool = FencePool::create(VulkanDevice),
                .swapchain = VulkanDevice->createSwapchain(headlessSwapchainParameters()),
                .frames    = std::make_unique<Graphics::Frame::FrameObjectManager>(
                    VulkanDevice, kFramesInFlight, 1u)};
    }
};

/**
 * A presentation keeps the swapchain it presented to. recreate() lets the old swapchain go, and it
 * dies only when the frame slot that presented to it starts again and waits the present fence.
 * The barrier is raw, so no command buffer holds the swapchain: only the presentation does.
 */
TEST_F(SwapchainTest, recreateRetiresOldSwapchainWhenSlotRestarts)
{
    if (!headlessSurfaceActive())
        GTEST_SKIP() << "VK_EXT_headless_surface not available";

    Rig rig = makeRig();

    const SwapchainGenerationWeakPtr old = rig.renderFrame(false);
    ASSERT_FALSE(old.expired());
    ASSERT_TRUE(rig.swapchain->recreate());
    EXPECT_FALSE(old.expired());

    // The frame that presented to the old swapchain is in the other slot; the next frame uses
    // the first slot and the one after reuses the slot of the old presentation.
    for (std::uint8_t frame = 1u; frame < kFramesInFlight; ++frame)
    {
        const SwapchainGenerationWeakPtr current = rig.renderFrame(false);
        EXPECT_FALSE(current.expired());
        EXPECT_FALSE(old.expired());
    }
    rig.renderFrame(false);
    EXPECT_TRUE(old.expired());

    EXPECT_TRUE(validationErrors.empty())
        << (validationErrors.empty() ? "" : validationErrors.front());
}

/**
 * The whole lifetime of a headless renderer: frames, two resizes, then teardown in the renderer's
 * order. The validation layer reports a semaphore destroyed while a present still waits it
 * (vkDestroySemaphore-05149) and a swapchain destroyed before its present fences signalled
 * (vkDestroySwapchainKHR-01282).
 */
TEST_F(SwapchainTest, shutdownAfterFramesAndRecreateRaisesNoValidationError)
{
    if (!validationActive() || !headlessSurfaceActive())
        GTEST_SKIP() << "validation layer or VK_EXT_headless_surface not available";

    {
        Rig rig = makeRig();
        for (int i = 0; i < 6; ++i)
        {
            rig.renderFrame(true);
            if (i % 2 == 1)
            {
                ASSERT_TRUE(rig.swapchain->recreate());
            }
        }
    }

    expectDeviceReleased(releaseDevice());
}

/**
 * The Swapchain keeps the device alive. The call under test is recreate(), which creates a new
 * swapchain from the old one.
 */
TEST_F(SwapchainTest, swapchainKeepsDeviceAlive)
{
    if (!headlessSurfaceActive())
        GTEST_SKIP() << "VK_EXT_headless_surface not available";

    auto       swapchain = VulkanDevice->createSwapchain(headlessSwapchainParameters());
    const auto released  = releaseDevice();

    expectDependentHoldsDevice(released,
                               swapchain,
                               [](Swapchain& swapchain)
                               {
                                   EXPECT_TRUE(swapchain.recreate());
                               });
}

/**
 * An AcquiredSwapchainImage keeps its swapchain, surface and device alive: the Swapchain can go
 * while the image is still recorded into a command buffer (requirement 8 of ResourceOwnership.md).
 */
TEST_F(SwapchainTest, acquiredImageKeepsSwapchainAlive)
{
    if (!headlessSurfaceActive())
        GTEST_SKIP() << "VK_EXT_headless_surface not available";

    auto swapchain = VulkanDevice->createSwapchain(headlessSwapchainParameters());
    auto acquire   = swapchain->acquireNextImage();
    ASSERT_TRUE(acquire.acquired);
    // The image is never presented. Its acquire signal still has to be consumed before the
    // semaphore may be destroyed.
    consumeAcquireSignal(VulkanDevice, acquire.imageAvailableSemaphore);
    auto acquired = std::move(acquire.acquired);
    acquire       = {};

    auto commandBuffer = std::make_unique<CommandBufferHandle>(
        VulkanDevice->createCommandBufferPool()->get(vk::CommandBufferLevel::ePrimary));

    swapchain.reset();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               acquired,
                               [&](AcquiredSwapchainImage& image)
                               {
                                   {
                                       CommandBufferRecorder recorder{***commandBuffer};
                                       recorder.setImageBarrier(acquired,
                                                                toPresentBarrier(image.getImage()));
                                       recorder.finish();
                                   }
                                   // Returning the command buffer drops what it retained.
                                   commandBuffer.reset();
                               });
}
} // namespace VOG::Tests
