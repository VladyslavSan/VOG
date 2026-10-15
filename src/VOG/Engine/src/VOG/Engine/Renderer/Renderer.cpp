#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/Assert.hpp>
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineCache.hpp"
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
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace VOG::Engine
{
static_assert(std::atomic<Extent>::is_always_lock_free);

namespace
{
constexpr std::chrono::milliseconds gZeroExtentBackoff{16};
/** Upper bound on one wait for a free image, and so on stop latency (plus one frame). */
constexpr std::chrono::milliseconds gAcquireTimeout{100};
constexpr std::size_t               gThreadId   = 0u;
constexpr std::array                gClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

vk::Extent2D
toVk(Extent extent)
{
    return vk::Extent2D{.width = extent.width, .height = extent.height};
}

/** Push constants the renderer fills: the matrix, then the material's parameters. */
constexpr std::size_t gPushConstantsSize = gParametersOffset + gMaxParametersSize;
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
          .extent         = toVk(config.extent),
      })}
    , mFrameObjectManager{std::make_unique<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice, frameSlotCount(mFramesInFlight, mSwapchain->getImageCount()), 1u)}
    , mRequestedExtent{config.extent}
    , mAppliedExtent{config.extent}
    , mPipelineCache{std::make_unique<PipelineCache>(mVulkanDevice)}
    , mPackets{std::make_unique<FramePacketQueue>()}
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
Renderer::resize(Extent extent)
{
    mRequestedExtent.store(extent);
}

MeshPtr
Renderer::createMesh(const MeshDescription& description)
{
    if (description.vertices.empty() || description.stride == 0u ||
        description.vertices.size() % description.stride != 0u)
    {
        throw std::invalid_argument{
            std::format("A mesh needs vertices, a non-zero stride and a size that is a multiple of "
                        "it; got {} bytes with stride {}.",
                        description.vertices.size(),
                        description.stride)};
    }
    if (description.attributes.size() > Graphics::Vulkan::Limits::gMaxNumStageAttributes)
    {
        throw std::invalid_argument{std::format("A mesh takes at most {} attributes; got {}.",
                                                Graphics::Vulkan::Limits::gMaxNumStageAttributes,
                                                description.attributes.size())};
    }
    for (const VertexAttribute& attribute : description.attributes)
    {
        if (std::uint64_t{attribute.offset} + sizeOf(attribute.format) > description.stride)
        {
            throw std::invalid_argument{std::format(
                "A vertex attribute at offset {} does not fit in the stride of {} bytes.",
                attribute.offset,
                description.stride)};
        }
    }

    Graphics::Vulkan::BufferPtr buffer = mVulkanDevice->createBuffer(
        {.size  = description.vertices.size_bytes(),
         .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = buffer->mapForWrite();
        std::memcpy(mapping.data, description.vertices.data(), description.vertices.size_bytes());
    }

    return std::make_shared<const Mesh>(Mesh{
        .buffer      = std::move(buffer),
        .vertexCount = static_cast<std::uint32_t>(description.vertices.size() / description.stride),
        .stride      = description.stride,
        .attributes  = description.attributes});
}

MaterialPtr
Renderer::createMaterial(MaterialDescription description)
{
    if (description.parameters.size() > gMaxParametersSize)
    {
        throw std::invalid_argument{
            std::format("A material takes at most {} bytes of parameters; got {}.",
                        gMaxParametersSize,
                        description.parameters.size())};
    }

    Graphics::Vulkan::ShaderProgramPtr program = mShaderProgramCache->get(description.shader);

    const std::size_t pushedSize = gParametersOffset + description.parameters.size();
    for (const vk::PushConstantRange& range : program->pushConstants.ranges)
    {
        if (std::size_t{range.offset} + range.size > pushedSize)
        {
            throw std::invalid_argument{std::format(
                "Shader \"{}\" has a push-constant range [{}, {}) past the {} bytes the renderer "
                "pushes for {} bytes of parameters.",
                description.shader,
                range.offset,
                range.offset + range.size,
                pushedSize,
                description.parameters.size())};
        }
    }

    return std::make_shared<const Material>(
        Material{.program = std::move(program), .parameters = std::move(description.parameters)});
}

void
Renderer::adoptPendingRenderables()
{
    const std::scoped_lock lock{mPendingMutex};
    mRenderables.insert(mRenderables.end(),
                        std::make_move_iterator(mPending.begin()),
                        std::make_move_iterator(mPending.end()));
    mPending.clear();
}

FramePacket
Renderer::makePacket(const FrameContext& frameContext)
{
    // Renderables that onDraw() adds land in mPending and join the next frame.
    adoptPendingRenderables();

    // A fresh vector of draws per frame, reuse its capacity if profiling shows the
    // allocation.
    FramePacket packet;
    for (const std::shared_ptr<Renderable>& renderable : mRenderables)
    {
        renderable->onDraw(frameContext, packet.draws);
    }

    // Here, not on the render thread: mPipelines has one entry per draw, in step with the draws.
    std::erase_if(packet.draws.mDraws,
                  [](const RenderQueue::Draw& draw)
                  {
                      return !draw.mesh || !draw.material;
                  });

    return packet;
}

void
Renderer::resolvePipelines(const RenderQueue& draws)
{
    mPipelines.clear();
    for (const RenderQueue::Draw& draw : draws.mDraws)
    {
        mPipelines.push_back(
            mPipelineCache->get(*draw.mesh, *draw.material, mSwapchain->getFormat()));
    }
}

bool
Renderer::applyExtent(Extent requested)
{
    if (!mSwapchain->recreate(toVk(requested)))
    {
        return false;
    }

    mAppliedExtent = requested;

    return true;
}

void
Renderer::renderFrame(FramePacket packet)
{
    using namespace VOG::Graphics::Vulkan;

    const Extent requested = mRequestedExtent.load();
    if (requested != mAppliedExtent && !applyExtent(requested))
    {
        // Zero-sized (minimized); back off instead of spinning.
        std::this_thread::sleep_for(gZeroExtentBackoff);
        return;
    }

    // Before acquiring: a vertex layout the shader cannot read throws while no image is acquired.
    resolvePipelines(packet.draws);

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

    const vk::Extent2D     extent = acquired->getExtent2D();
    const Math::Matrix4x4f viewProjection =
        packet.draws.mCamera.viewProjection({.width = extent.width, .height = extent.height});

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

            for (std::size_t i = 0u; i < packet.draws.mDraws.size(); ++i)
            {
                const RenderQueue::Draw& draw     = packet.draws.mDraws[i];
                const Material&          material = *draw.material;

                // The matrix first, then the parameters; createMaterial checked that the shader's
                // ranges end inside what is filled here.
                const Math::Matrix4x4f                    mvp = viewProjection * draw.world;
                std::array<std::byte, gPushConstantsSize> pushConstants{};
                std::memcpy(pushConstants.data(), &mvp, sizeof(mvp));
                std::ranges::copy(material.parameters, pushConstants.begin() + gParametersOffset);

                pass.bindPipeline(mPipelines[i]);
                for (const vk::PushConstantRange& range : material.program->pushConstants.ranges)
                {
                    pass.pushConstants<std::byte>(
                        *material.program->pipelineLayout,
                        range.stageFlags,
                        range.offset,
                        vk::ArrayProxy<const std::byte>{range.size,
                                                        pushConstants.data() + range.offset});
                }
                pass.bindVertexBuffers(0u, {{.buffer = draw.mesh->buffer, .offset = 0u}});
                pass.draw(draw.mesh->vertexCount, 1, 0, 0);
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
                                 std::optional<FramePacket> packet = mPackets->pop(stopToken);
                                 if (!packet)
                                 {
                                     break;
                                 }
                                 renderFrame(std::move(*packet));
                             }
                             exit.set_value();
                         }
                         catch (...)
                         {
                             // Report first, so getState() shows the failure at once. Then keep
                             // consuming until stop(): submitFrame() must not wait for a
                             // consumer that is gone.
                             exit.set_exception(std::current_exception());
                             while (mPackets->pop(stopToken))
                             {
                             }
                         }
                     }};
}

void
Renderer::submitFrame(const FrameContext& frameContext)
{
    std::stop_token stopToken;
    {
        const std::scoped_lock lock{mStateMutex};
        if (!mRenderThread.joinable())
        {
            return;
        }
        // Not mStateMutex across the push: stop() holds it across the join, and a blocked push
        // is what stop() wakes through this token.
        stopToken = mRenderThread.get_stop_token();
    }

    mPackets->push(makePacket(frameContext), stopToken);
}

void
Renderer::trySubmitFrame(const FrameContext& frameContext)
{
    if (getState() == State::eStopped || !mPackets->empty())
    {
        return;
    }

    mPackets->tryPush(makePacket(frameContext));
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
