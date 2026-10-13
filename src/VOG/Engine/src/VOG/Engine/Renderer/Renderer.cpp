#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/Assert.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <span>
#include <stdexcept>
#include <thread>
#include <typeinfo>
#include <utility>

namespace VOG::Engine
{
namespace
{
constexpr std::chrono::milliseconds gZeroExtentBackoff{16};
/** Upper bound on one wait for a free image, and so on stop latency (plus one frame). */
constexpr std::chrono::milliseconds gAcquireTimeout{100};
constexpr std::size_t               gThreadId   = 0u;
constexpr std::array                gClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

Renderer::Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config)
    : mFramesInFlight{std::clamp(config.framesInFlight, std::uint8_t{1}, kMaxFramesInFlight)}
    , mVulkanInstance{Graphics::Vulkan::Instance::create({
          .appName    = config.appName,
          .engineName = config.engineName,
          .layers     = config.layers,
          .extensions = config.extensions,
      })}
    , mVulkanDevice{mVulkanInstance->makeDevice()}
    , mGraphicsQueue{mVulkanDevice->getGraphicsQueue()}
    , mFencePool{Graphics::Vulkan::FencePool::create(mVulkanDevice)}
    , mShaderProgramCache{std::make_shared<Graphics::ShaderProgramCache>(mVulkanDevice,
                                                                         config.shaderSourcePath)}
    , mSwapchain{mVulkanDevice->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = mFramesInFlight,
          .surface        = surfaceHandle,
          .extent         = config.extent,
      })}
    , mFrameObjectManager{std::make_unique<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice, frameSlotCount(mFramesInFlight, mSwapchain->getImageCount()), 1u)}
    , mStartTime{std::chrono::steady_clock::now()}
    , mRequestedExtent{config.extent}
    , mAppliedExtent{config.extent}
{
}

Renderer::~Renderer()
{
    // stop() is noexcept and joins the thread; the members below are destroyed after it.
    if (const std::exception_ptr failure = stop())
    {
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::exception& exception)
        {
            spdlog::error("The render thread failed and nobody collected it: {}", exception.what());
        }
        catch (...)
        {
            spdlog::error("The render thread failed with an unknown exception.");
        }
    }
}

std::uint8_t
Renderer::frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount)
{
    if (framesInFlight > imageCount)
    {
        spdlog::warn(
            "frames_in_flight ({}) exceeds swapchain image count ({}), clamping frame slots to {}",
            framesInFlight,
            imageCount,
            imageCount);
    }

    return static_cast<std::uint8_t>(std::min<std::size_t>(framesInFlight, imageCount));
}

void
Renderer::addRenderable(std::shared_ptr<Renderable> renderable)
{
    if (!renderable)
    {
        return;
    }

    const std::scoped_lock lock{mPendingMutex};
    mPending.push_back(std::move(renderable));
}

void
Renderer::resize(vk::Extent2D extent)
{
    mRequestedExtent.store(extent);
}

std::shared_ptr<Renderable>
Renderer::takePendingRenderable()
{
    const std::scoped_lock lock{mPendingMutex};
    if (mPending.empty())
    {
        return nullptr;
    }

    std::shared_ptr<Renderable> renderable = std::move(mPending.front());
    mPending.pop_front();

    return renderable;
}

void
Renderer::prepareRenderables()
{
    std::shared_ptr<Renderable> renderable = takePendingRenderable();
    if (!renderable)
    {
        return;
    }

    const ResourceContext resourceContext{
        mVulkanDevice, mShaderProgramCache, mSwapchain->getFormat()};

    // One at a time and without a lock: prepare() may call addRenderable(), and the child is
    // taken by this same loop. A prepare() that throws drops only its own renderable; the rest
    // stay pending.
    while (renderable)
    {
        renderable->prepare(resourceContext);
        mRenderables.push_back(std::move(renderable));
        renderable = takePendingRenderable();
    }
}

void
Renderer::collectRenderItems(const FrameContext& frameContext)
{
    mRenderItems.clear();

    for (const std::shared_ptr<Renderable>& renderable : mRenderables)
    {
        Renderable&       current = *renderable;
        const std::size_t before  = mRenderItems.size();
        current.collect(frameContext, mRenderItems);

        for (std::size_t i = before; i < mRenderItems.size(); ++i)
        {
            const RenderItem& item = mRenderItems[i];
            if (!item.pipeline || !item.vertexBuffer || item.vertexCount == 0u)
            {
                throw std::invalid_argument{
                    std::format("{} produced a RenderItem without a pipeline, a vertex buffer or "
                                "a vertex count.",
                                typeid(current).name())};
            }
        }
    }
}

bool
Renderer::applyExtent(vk::Extent2D requested)
{
    if (!mSwapchain->recreate(requested))
    {
        return false;
    }

    mAppliedExtent = requested;

    return true;
}

void
Renderer::renderFrame()
{
    using namespace VOG::Graphics::Vulkan;

    prepareRenderables();

    const vk::Extent2D requested = mRequestedExtent.load();
    if (requested != mAppliedExtent && !applyExtent(requested))
    {
        // Zero-sized (minimized); back off instead of spinning.
        std::this_thread::sleep_for(gZeroExtentBackoff);
        return;
    }

    // Collect and check before acquiring: a renderable that throws, or hands back a malformed
    // item, must do so while no image is acquired. The frame's extent is the size of every image
    // acquired from here on, because an out-of-date acquire drops the frame.
    const vk::Extent2D extent = mSwapchain->getExtent();
    collectRenderItems({
        .timeSeconds =
            std::chrono::duration<double>{std::chrono::steady_clock::now() - mStartTime}.count(),
        .extent = extent,
    });

    const auto acquireResult = mSwapchain->acquireNextImage(gAcquireTimeout);
    if (acquireResult.status == Swapchain::AcquireStatus::eOutOfDate)
    {
        if (!applyExtent(mRequestedExtent.load()))
        {
            std::this_thread::sleep_for(gZeroExtentBackoff);
        }
        return;
    }
    if (acquireResult.status == Swapchain::AcquireStatus::eSkip)
    {
        // The acquire waited up to gAcquireTimeout, so this does not spin.
        return;
    }

    const auto& acquired = acquireResult.acquired;
    VOG_ASSERT_MSG(acquired, "eReady acquire must provide AcquiredSwapchainImage.");

    // Advance the frame slot only when we will actually submit — skip/out-of-date must not
    // burn a fence-keyed release slot.
    auto frame = mFrameObjectManager->acquireNextFrame();
    auto pool  = frame->getCommandBufferPoolForThread(gThreadId);

    auto recorder = pool->record();

    {
        recorder.setImageBarrier(
            acquired,
            {.srcStageMask        = vk::PipelineStageFlagBits2::eBottomOfPipe,
             .dstStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
             .dstAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
             .oldLayout           = vk::ImageLayout::eUndefined,
             .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .subresourceRange    = {
                 .aspectMask     = vk::ImageAspectFlagBits::eColor,
                 .baseMipLevel   = 0,
                 .levelCount     = VK_REMAINING_MIP_LEVELS,
                 .baseArrayLayer = 0,
                 .layerCount     = VK_REMAINING_ARRAY_LAYERS,
             }});

        {
            auto pass = recorder.beginRendering({{.attachment = acquired,
                                                  .loadOp     = vk::AttachmentLoadOp::eClear,
                                                  .storeOp    = vk::AttachmentStoreOp::eStore,
                                                  .clearValue = vk::ClearColorValue{gClearColor}}});

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

            for (const RenderItem& item : mRenderItems)
            {
                pass.bindPipeline(item.pipeline);
                pass.bindVertexBuffers(0u, {{.buffer = item.vertexBuffer, .offset = 0u}});

                if (!item.pushConstants.empty())
                {
                    pass.pushConstants<std::byte>(
                        *item.pipeline->program->pipelineLayout,
                        item.pushConstantsStages,
                        0u,
                        vk::ArrayProxy<const std::byte>{
                            static_cast<std::uint32_t>(item.pushConstants.size()),
                            item.pushConstants.data()});
                }

                pass.draw(item.vertexCount, 1, 0, 0);
            }
        }

        recorder.setImageBarrier(
            acquired,
            {.srcStageMask        = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
             .srcAccessMask       = vk::AccessFlagBits2::eColorAttachmentWrite,
             .dstStageMask        = vk::PipelineStageFlagBits2::eTopOfPipe,
             .oldLayout           = vk::ImageLayout::eColorAttachmentOptimal,
             .newLayout           = vk::ImageLayout::ePresentSrcKHR,
             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
             .subresourceRange    = {
                 .aspectMask     = vk::ImageAspectFlagBits::eColor,
                 .baseMipLevel   = 0,
                 .levelCount     = VK_REMAINING_MIP_LEVELS,
                 .baseArrayLayer = 0,
                 .layerCount     = VK_REMAINING_ARRAY_LAYERS,
             }});
    }

    auto executable = recorder.finish();

    mGraphicsQueue->submit(*mFencePool,
                           std::array{Queue::SemaphoreWait{
                               .semaphore = acquired->imageAvailableSemaphore(),
                               .stage     = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
                           std::span{&executable, 1u},
                           std::array{frame->getRenderFinishedSemaphore()});

    auto presented =
        mSwapchain->present(acquired, frame->getRenderFinishedSemaphore(), *mFencePool);
    frame->retainUntilPresented(std::move(presented.presentation));

    if (presented.status == Swapchain::PresentStatus::eOutOfDate &&
        !applyExtent(mRequestedExtent.load()))
    {
        std::this_thread::sleep_for(gZeroExtentBackoff);
    }
}

void
Renderer::start()
{
    const std::scoped_lock lock{mStateMutex};
    if (mRenderThread.joinable())
    {
        throw std::logic_error{"Renderer::start() needs a stopped renderer."};
    }

    std::promise<void> exit;
    mExit = exit.get_future();
    mRenderThread =
        std::jthread{[this, exit = std::move(exit)](const std::stop_token& stopToken) mutable
                     {
                         try
                         {
                             while (!stopToken.stop_requested())
                             {
                                 renderFrame();
                             }
                             exit.set_value();
                         }
                         catch (...)
                         {
                             exit.set_exception(std::current_exception());
                         }
                     }};
}

std::exception_ptr
Renderer::stop() noexcept
{
    const std::scoped_lock lock{mStateMutex};
    if (!mRenderThread.joinable())
    {
        return nullptr;
    }

    mRenderThread.request_stop();
    mRenderThread.join();

    try
    {
        mExit.get();
    }
    catch (...)
    {
        return std::current_exception();
    }

    return nullptr;
}

Renderer::State
Renderer::getState() const
{
    const std::scoped_lock lock{mStateMutex};
    if (!mRenderThread.joinable())
    {
        return State::eStopped;
    }

    // A thread only ends normally after stop() asked for it, and stop() holds the lock until the
    // join is done, so a ready future seen here is a failure.
    return mExit.wait_for(std::chrono::seconds{0}) == std::future_status::ready ? State::eFailed
                                                                                : State::eRunning;
}
} // namespace VOG::Engine
