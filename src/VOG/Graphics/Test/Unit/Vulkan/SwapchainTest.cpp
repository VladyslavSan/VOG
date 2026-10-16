#include "VOG/Graphics/Vulkan/Attachment/Swapchain.hpp"

#include "VOG/Graphics/Frame/FrameObjectManager.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"
#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "VulkanFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
constexpr vk::Extent2D gExtent{.width = 64u, .height = 64u};

/** A headless surface has a free image at once; the timeout only guards against a hang. */
constexpr std::chrono::seconds gAcquireTimeout{1};

/** Takes a freshly acquired image to the layout vkQueuePresentKHR expects. */
vk::ImageMemoryBarrier2
toPresentBarrier()
{
    return {.srcStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
            .dstStageMask        = vk::PipelineStageFlagBits2::eBottomOfPipe,
            .oldLayout           = vk::ImageLayout::eUndefined,
            .newLayout           = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
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
    auto fencePool  = FencePool::create(device);
    auto pool       = device->createCommandBufferPool();
    auto executable = recordEmpty(pool);

    device->getGraphicsQueue()->submit(
        *fencePool,
        std::array{Queue::SemaphoreWait{.semaphore = imageAvailable,
                                        .stage     = vk::PipelineStageFlagBits2::eTopOfPipe}},
        std::span{&executable, 1u},
        {});
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
     * @return the acquire semaphore of the frame's image: its command buffer retains it until the
     *         slot restarts, and the swapchain generation holds it until the generation is gone.
     */
    std::weak_ptr<Semaphore>
    renderFrame()
    {
        auto acquireResult = swapchain->acquireNextImage(gAcquireTimeout);
        EXPECT_EQ(acquireResult.status, Swapchain::AcquireStatus::eReady);
        const AcquiredSwapchainImagePtr acquired = acquireResult.acquired;
        if (!acquired)
        {
            return {};
        }

        auto frame    = frames->acquireNextFrame();
        auto recorder = frame->getCommandBufferPoolForThread(0u)->record();
        recorder.setImageBarrier(acquired, toPresentBarrier());
        auto executable = recorder.finish();

        queue->submit(*fencePool,
                      std::array{Queue::SemaphoreWait{
                          .semaphore = acquired->imageAvailableSemaphore(),
                          .stage     = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
                      std::span{&executable, 1u},
                      std::array{frame->getRenderFinishedSemaphore()});

        auto presented =
            swapchain->present(acquired, frame->getRenderFinishedSemaphore(), *fencePool);
        EXPECT_EQ(presented.status, Swapchain::PresentStatus::eReady);

        frame->retainUntilPresented(std::move(presented.presentation));
        return acquired->imageAvailableSemaphore();
    }
};
} // namespace

/** Swapchains on a headless surface, under the validation layer when it is installed. */
class SwapchainTest : public VulkanFixture
{
public:
    SwapchainTest()
        : VulkanFixture{Options{.validation = true, .headlessSurface = true}}
    {
    }

protected:
    static constexpr std::uint8_t kFramesInFlight = 2u;

    void
    SetUp() override
    {
        if (!headlessSurfaceActive())
        {
            GTEST_SKIP() << "VK_EXT_headless_surface not available";
        }
    }

    /** Parameters of a swapchain on a headless surface. */
    static Swapchain::SwapchainParameters
    headlessSwapchainParameters()
    {
        return {.framesInFlight = kFramesInFlight,
                .surface        = {.platform = Common::SurfaceHandle::Platform::eHeadless},
                .extent         = gExtent};
    }

    Rig
    makeRig()
    {
        return {.queue     = mDevice->getGraphicsQueue(),
                .fencePool = FencePool::create(mDevice),
                .swapchain = mDevice->createSwapchain(headlessSwapchainParameters()),
                .frames    = std::make_unique<Graphics::Frame::FrameObjectManager>(
                    mDevice, kFramesInFlight, 1u)};
    }
};

/**
 * The old generation is not visible from outside, so its acquire semaphore stands in for it: the
 * generation holds it, and so does the command buffer that waited it until its pool resets. The
 * semaphore must live until the frame slot that presented to the old swapchain restarts, and not
 * past it: then the presentation and the command buffer are dropped together.
 */
TEST_F(SwapchainTest, recreateRetiresOldSwapchainWhenSlotRestarts)
{
    Rig rig = makeRig();

    const std::weak_ptr<Semaphore> old = rig.renderFrame();
    ASSERT_FALSE(old.expired());
    ASSERT_TRUE(rig.swapchain->recreate(gExtent));
    EXPECT_FALSE(old.expired());

    // The frame that presented to the old swapchain is in the other slot; the next frame uses
    // the first slot and the one after reuses the slot of the old presentation.
    for (std::uint8_t frame = 1u; frame < kFramesInFlight; ++frame)
    {
        rig.renderFrame();
        EXPECT_FALSE(old.expired());
    }
    rig.renderFrame();
    EXPECT_TRUE(old.expired());

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * A presentation keeps the swapchain it presented to. Here nothing else holds the old generation's
 * acquire semaphore once the pool is reset, so it is a proxy for the generation alone: it must
 * survive recreate() and expire exactly when the presentation is dropped.
 */
TEST_F(SwapchainTest, presentationKeepsRecreatedSwapchainUntilDropped)
{
    auto swapchain      = mDevice->createSwapchain(headlessSwapchainParameters());
    auto fencePool      = FencePool::create(mDevice);
    auto pool           = mDevice->createCommandBufferPool();
    auto renderFinished = mDevice->createSemaphore();

    std::weak_ptr<Semaphore>               imageAvailable;
    std::optional<Swapchain::Presentation> presentation;
    {
        auto acquire = swapchain->acquireNextImage(gAcquireTimeout);
        ASSERT_EQ(acquire.status, Swapchain::AcquireStatus::eReady);
        imageAvailable = acquire.acquired->imageAvailableSemaphore();

        auto recorder = pool->record();
        recorder.setImageBarrier(acquire.acquired, toPresentBarrier());
        auto executable = recorder.finish();

        mDevice->getGraphicsQueue()->submit(
            *fencePool,
            std::array{
                Queue::SemaphoreWait{.semaphore = acquire.acquired->imageAvailableSemaphore(),
                                     .stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
            std::span{&executable, 1u},
            std::array{renderFinished});

        presentation.emplace(std::move(
            swapchain->present(acquire.acquired, renderFinished, *fencePool).presentation));
    }
    // The command buffer let go of the semaphore and of the acquired image.
    pool->reset();
    EXPECT_FALSE(imageAvailable.expired());

    ASSERT_TRUE(swapchain->recreate(gExtent));
    EXPECT_FALSE(imageAvailable.expired());

    presentation.reset();
    EXPECT_TRUE(imageAvailable.expired());

    EXPECT_TRUE(mValidationErrors.empty())
        << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/** The size passed to recreate() is the size of the new images where the application chooses. */
TEST_F(SwapchainTest, recreateUsesTheRequestedExtentAndSkipsAZeroOne)
{
    auto swapchain = mDevice->createSwapchain(headlessSwapchainParameters());
    ASSERT_TRUE(swapchain->recreate({.width = 128u, .height = 128u}));

    auto acquire = swapchain->acquireNextImage(gAcquireTimeout);
    ASSERT_EQ(acquire.status, Swapchain::AcquireStatus::eReady);
    EXPECT_EQ(acquire.acquired->getExtent2D().width, 128u);
    EXPECT_EQ(acquire.acquired->getExtent2D().height, 128u);
    consumeAcquireSignal(mDevice, acquire.acquired->imageAvailableSemaphore());

    // A minimized window: nothing to build, and no exception for the render loop to die on.
    EXPECT_FALSE(swapchain->recreate({.width = 0u, .height = 0u}));
    EXPECT_FALSE(swapchain->recreate({.width = 0u, .height = 128u}));
}

/** Where the surface leaves the size to the application, the application must give one. */
TEST_F(SwapchainTest, constructionNeedsAnExtentWhereTheApplicationChooses)
{
    auto parameters   = headlessSwapchainParameters();
    parameters.extent = vk::Extent2D{.width = 0u, .height = 0u};

    try
    {
        auto swapchain = mDevice->createSwapchain(parameters);
        ADD_FAILURE() << "A zero extent was accepted.";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string{error.what()}.find("SwapchainParameters::extent"), std::string::npos)
            << error.what();
    }
}

/**
 * The whole lifetime of a headless renderer: frames, two resizes, then teardown in the renderer's
 * order. The validation layer reports a semaphore destroyed while a present still waits it
 * (vkDestroySemaphore-05149) and a swapchain destroyed before its present fences signalled
 * (vkDestroySwapchainKHR-01282).
 */
TEST_F(SwapchainTest, shutdownAfterFramesAndRecreateRaisesNoValidationError)
{
    if (!validationActive())
        GTEST_SKIP() << "VK_LAYER_KHRONOS_validation not available";

    {
        Rig rig = makeRig();
        for (int i = 0; i < 6; ++i)
        {
            rig.renderFrame();
            if (i % 2 == 1)
            {
                ASSERT_TRUE(rig.swapchain->recreate(gExtent));
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
    auto       swapchain = mDevice->createSwapchain(headlessSwapchainParameters());
    const auto released  = releaseDevice();

    expectDependentHoldsDevice(released,
                               swapchain,
                               [](Swapchain& swapchain)
                               {
                                   EXPECT_TRUE(swapchain.recreate(gExtent));
                               });
}

/**
 * An AcquiredSwapchainImage keeps its swapchain, surface and device alive: the Swapchain can go
 * while the image is still recorded into a command buffer (requirement 8 of ResourceOwnership.md).
 */
TEST_F(SwapchainTest, acquiredImageKeepsSwapchainAlive)
{
    auto swapchain = mDevice->createSwapchain(headlessSwapchainParameters());
    auto acquire   = swapchain->acquireNextImage(gAcquireTimeout);
    ASSERT_TRUE(acquire.acquired);
    // The image is never presented. Its acquire signal still has to be consumed before the
    // semaphore may be destroyed.
    consumeAcquireSignal(mDevice, acquire.acquired->imageAvailableSemaphore());
    auto acquired = std::move(acquire.acquired);
    acquire       = {};

    auto pool = mDevice->createCommandBufferPool();

    swapchain.reset();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               acquired,
                               [&](AcquiredSwapchainImage&)
                               {
                                   {
                                       auto recorder = pool->record();
                                       recorder.setImageBarrier(acquired, toPresentBarrier());
                                       auto executable = recorder.finish();
                                   }
                                   // Dropping the unsubmitted command buffer released what it
                                   // retained; dropping the pool lets go of the device.
                                   pool.reset();
                               });
}
} // namespace VOG::Tests
