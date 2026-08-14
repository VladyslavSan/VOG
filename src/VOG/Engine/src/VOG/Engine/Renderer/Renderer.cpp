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
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <thread>
#include <utility>

namespace VOG::Engine
{
namespace
{
constexpr std::chrono::milliseconds kZeroExtentBackoff{16};
constexpr std::array                kClearColor = {0.0f, 0.0f, 0.0f, 1.0f};
} // namespace

Renderer::~Renderer()
{
    try
    {
        requestRenderChangeState(RenderJobState::eInactive);
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
    }
}

Renderer::Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config)
    : mCurrentState{RenderJobState::eInactive}
    , mMaxFramesInFlight{std::clamp(config.framesInFlight, std::uint8_t{1}, kMaxFramesInFlight)}
    , mVulkanInstance{Graphics::Vulkan::Instance::create({
          .appName    = config.appName,
          .engineName = config.engineName,
          .layers     = config.layers,
          .extensions = config.extensions,
      })}
    , mVulkanDevice{mVulkanInstance->makeDevice()}
    , mShaderProgramCache{std::make_shared<Graphics::ShaderProgramCache>(mVulkanDevice,
                                                                         config.shaderSourcePath)}
    , mSwapchain{mVulkanDevice->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = mMaxFramesInFlight,
          .surface        = surfaceHandle,
      })}
    , mFrameObjectManager{std::make_shared<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice,
          std::min<std::size_t>(mMaxFramesInFlight, mSwapchain->getImageCount()),
          1u)}
    , mStartTime{std::chrono::steady_clock::now()}
{
    const std::size_t imageCount = mSwapchain->getImageCount();
    if (mMaxFramesInFlight > imageCount)
    {
        spdlog::warn(
            "frames_in_flight ({}) exceeds swapchain image count ({}), clamping frame slots to {}",
            mMaxFramesInFlight,
            imageCount,
            imageCount);
        mMaxFramesInFlight = static_cast<std::uint8_t>(imageCount);
    }
}

void
Renderer::addRenderable(std::shared_ptr<Renderable> renderable)
{
    if (!renderable)
    {
        return;
    }

    const std::scoped_lock lock{mRenderablesMutex};
    mRenderables.push_back({.renderable = std::move(renderable)});
}

void
Renderer::clearRenderables()
{
    const std::scoped_lock lock{mRenderablesMutex};
    mRenderables.clear();
}

void
Renderer::prepareRenderables()
{
    const std::scoped_lock lock{mRenderablesMutex};

    ResourceContext resourceContext{mVulkanDevice, mShaderProgramCache, mSwapchain->getFormat()};

    for (RenderableEntry& entry : mRenderables)
    {
        if (!entry.prepared)
        {
            entry.renderable->prepare(resourceContext);
            entry.prepared = true;
        }
    }
}

void
Renderer::collectRenderItems(const FrameContext& frameContext)
{
    mRenderItems.clear();

    const std::scoped_lock lock{mRenderablesMutex};
    for (const RenderableEntry& entry : mRenderables)
    {
        entry.renderable->collect(frameContext, mRenderItems);
    }
}

void
Renderer::render()
{
    using namespace VOG::Graphics::Vulkan;
    constexpr std::size_t threadId = 0u;

    prepareRenderables();

    const auto acquireResult = mSwapchain->acquireNextImage();
    if (acquireResult.status == Swapchain::AcquireStatus::eOutOfDate)
    {
        if (!mSwapchain->recreate())
        {
            // Surface is zero-sized (minimized); back off instead of spinning.
            std::this_thread::sleep_for(kZeroExtentBackoff);
        }
        return;
    }
    if (acquireResult.status == Swapchain::AcquireStatus::eSkip)
    {
        return;
    }

    const auto& acquired = acquireResult.acquired;
    VOG_ASSERT_MSG(acquired, "eReady acquire must provide AcquiredSwapchainImage.");

    const auto extent = acquired->getExtent();

    collectRenderItems({
        .timeSeconds =
            std::chrono::duration<double>{std::chrono::steady_clock::now() - mStartTime}.count(),
        .extent     = acquired->getExtent2D(),
        .frameIndex = mFrameIndex,
    });

    // Advance the frame slot only when we will actually submit — skip/out-of-date must not
    // burn a fence-keyed release slot.
    auto frame = mFrameObjectManager->acquireNextFrame();
    auto pool  = frame->getCommandBufferPoolForThread(threadId);

    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
    commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    Graphics::Vulkan::CommandBufferRecorder recorder{*mVulkanDevice, **commandBuffer};

    recorder.setImageBarrier(acquired,
                             {.srcStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
                              .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                              .oldLayout    = vk::ImageLayout::eUndefined,
                              .newLayout    = vk::ImageLayout::eColorAttachmentOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image               = acquired->getImage(),
                              .subresourceRange    = {
                                     .aspectMask     = vk::ImageAspectFlagBits::eColor,
                                     .baseMipLevel   = 0,
                                     .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                     .baseArrayLayer = 0,
                                     .layerCount     = VK_REMAINING_ARRAY_LAYERS,
                              }});

    {
        recorder.beginRendering({{.attachment = acquired,
                                  .loadOp     = vk::AttachmentLoadOp::eClear,
                                  .storeOp    = vk::AttachmentStoreOp::eStore,
                                  .clearValue = vk::ClearColorValue{kClearColor}}});

        recorder.setViewport(0,
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
        recorder.setScissor(0,
                            {
                                {
                                    .offset = {.x = 0, .y = 0},
                                    .extent = {.width = extent.width, .height = extent.height},
                                },
                            });

        for (const RenderItem& item : mRenderItems)
        {
            if (!item.pipeline || !item.vertexBuffer || item.vertexCount == 0u)
            {
                continue;
            }

            recorder.bindPipeline(item.pipeline);
            recorder.bindVertexBuffers(0u, {{.buffer = item.vertexBuffer, .offset = 0u}});

            if (!item.pushConstants.empty())
            {
                recorder.pushConstants<std::byte>(
                    *item.pipeline->program->pipelineLayout,
                    item.pushConstantsStages,
                    0u,
                    vk::ArrayProxy<const std::byte>{
                        static_cast<std::uint32_t>(item.pushConstants.size()),
                        item.pushConstants.data()});
            }

            recorder.draw(item.vertexCount, 1, 0, 0);
        }

        recorder.endRendering();
    }

    recorder.setImageBarrier(acquired,
                             {.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                              .dstStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                              .oldLayout    = vk::ImageLayout::eColorAttachmentOptimal,
                              .newLayout    = vk::ImageLayout::ePresentSrcKHR,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .image               = acquired->getImage(),
                              .subresourceRange    = {
                                     .aspectMask     = vk::ImageAspectFlagBits::eColor,
                                     .baseMipLevel   = 0,
                                     .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                     .baseArrayLayer = 0,
                                     .layerCount     = VK_REMAINING_ARRAY_LAYERS,
                              }});

    commandBuffer->end();

    mVulkanDevice->graphicsQueue.submit(
        std::array{*acquired->getImageAvailableSemaphore()},
        std::array{vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput}},
        std::array{std::move(commandBuffer)},
        std::array{*frame->getRenderFinishedSemaphore()});

    ++mFrameIndex;

    if (mSwapchain->present(*frame->getRenderFinishedSemaphore()) ==
        Swapchain::PresentStatus::eOutOfDate)
    {
        if (!mSwapchain->recreate())
        {
            std::this_thread::sleep_for(kZeroExtentBackoff);
        }
    }
}

Renderer::RenderJobState
Renderer::getRenderJobState() const
{
    return mCurrentState.load();
}

bool
Renderer::requestRenderChangeState(RenderJobState newState)
{
    const std::scoped_lock lock{mStateMutex};

    if (newState == mCurrentState.load())
    {
        return false;
    }

    switch (newState)
    {
    case RenderJobState::eActive:
    {
        mRenderThread = std::jthread{
            [renderer = weak_from_this()](
                std::stop_token stopToken) // NOLINT(performance-unnecessary-value-param)
            {
                try
                {
                    while (!stopToken.stop_requested())
                    {
                        auto locked = renderer.lock();
                        if (!locked)
                        {
                            break;
                        }
                        locked->render();
                    }
                }
                catch (const std::exception& exception)
                {
                    spdlog::error("Render thread stopped due to exception: {}", exception.what());
                }
                catch (...)
                {
                    spdlog::error("Render thread stopped due to an unknown exception.");
                }

                if (auto locked = renderer.lock())
                {
                    locked->mCurrentState.store(RenderJobState::eInactive);
                }
            }};
        break;
    }
    case RenderJobState::eInactive:
    {
        // Move-assigning an empty thread requests stop on the old one and joins it.
        mRenderThread = std::jthread{};
        break;
    }
    }

    mCurrentState.store(newState);

    return true;
}
} // namespace VOG::Engine
