#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/Assert.hpp>
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineCache.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/AcquiredSwapchainImage.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/UniformBlock.hpp>

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
#include <variant>

namespace VOG::Engine
{
static_assert(std::atomic<Extent>::is_always_lock_free);

namespace
{
constexpr std::chrono::milliseconds gZeroExtentBackoff{16};
/**
 * Upper bound on one wait for a free image. Stop latency is this plus one frame, plus the fence
 * waits of FrameObjectManager::prepareNextFrame, which have no timeout: they assume that the GPU
 * and the presentation engine make progress.
 */
constexpr std::chrono::milliseconds gAcquireTimeout{100};
constexpr std::size_t               gThreadId   = 0u;
constexpr std::array                gClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

vk::Extent2D
toVk(Extent extent)
{
    return vk::Extent2D{.width = extent.width, .height = extent.height};
}

constexpr std::size_t
alignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1u) / alignment * alignment;
}

/**
 * Images and uniform blocks the descriptor heap holds at once.
 * Fixed, because growing means binding another heap; make it grow when a scene runs out.
 */
constexpr std::uint32_t gDescriptorHeapSlots = 4096u;

/** The one sampler of the sampler heap, at index 0. */
const vk::SamplerCreateInfo gLinearRepeatSampler{.magFilter    = vk::Filter::eLinear,
                                                 .minFilter    = vk::Filter::eLinear,
                                                 .mipmapMode   = vk::SamplerMipmapMode::eLinear,
                                                 .addressModeU = vk::SamplerAddressMode::eRepeat,
                                                 .addressModeV = vk::SamplerAddressMode::eRepeat,
                                                 .addressModeW = vk::SamplerAddressMode::eRepeat,
                                                 .maxLod       = vk::LodClampNone};
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
    , mTransferQueue{mVulkanDevice->getTransferQueue()}
    , mFencePool{Graphics::Vulkan::FencePool::create(mVulkanDevice)}
    , mShaderProgramCache{std::make_shared<Graphics::ShaderProgramCache>(mVulkanDevice,
                                                                         config.shaderSourcePath)}
    , mHeap{mVulkanDevice->createDescriptorHeap(gDescriptorHeapSlots, {&gLinearRepeatSampler, 1u})}
    , mSwapchain{mVulkanDevice->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = mFramesInFlight,
          .surface        = surfaceHandle,
          .extent         = toVk(config.extent),
      })}
    , mFrameObjectManager{std::make_unique<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice, frameSlotCount(mFramesInFlight, mSwapchain->getImageCount()), 1u)}
    , mRequestedExtent{config.extent}
    , mAppliedExtent{config.extent}
    , mUploadTimeline{mVulkanDevice->createTimelineSemaphore()}
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

    // More slots than images would only queue frames behind the images, and so add latency.
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

    const auto vertexCount =
        static_cast<std::uint32_t>(description.vertices.size() / description.stride);
    if (description.indices.size() % 3u != 0u)
    {
        throw std::invalid_argument{std::format(
            "A mesh draws triangles, so its index count must be a multiple of 3; got {}.",
            description.indices.size())};
    }
    for (std::size_t i = 0u; i < description.indices.size(); ++i)
    {
        if (description.indices[i] >= vertexCount)
        {
            throw std::invalid_argument{std::format("Index {} is {}, but the mesh has {} vertices.",
                                                    i,
                                                    description.indices[i],
                                                    vertexCount)};
        }
    }

    // The indices follow the vertices, aligned to the size of an index.
    const std::size_t indexOffset =
        alignUp(description.vertices.size_bytes(), sizeof(std::uint32_t));

    const std::size_t size = indexOffset + description.indices.size_bytes();

    // The staging buffer takes the bytes on this thread; the render thread copies them into the
    // device-local buffer, which only the GPU reads.
    Graphics::Vulkan::BufferPtr staging = mVulkanDevice->createBuffer(
        {.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::memcpy(mapping.data, description.vertices.data(), description.vertices.size_bytes());
        if (!description.indices.empty())
        {
            std::memcpy(mapping.data + indexOffset,
                        description.indices.data(),
                        description.indices.size_bytes());
        }
    }

    Graphics::Vulkan::BufferPtr buffer = mVulkanDevice->createBuffer(
        {.size  = size,
         .usage = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer |
                  vk::BufferUsageFlagBits::eTransferDst},
        {});

    // After the buffer exists, so that a frame which draws the mesh finds its upload.
    {
        const std::scoped_lock lock{mUploadMutex};
        mPendingUploads.push_back({.staging = std::move(staging), .destination = buffer});
    }

    return std::make_shared<const Mesh>(
        Mesh{.buffer      = std::move(buffer),
             .vertexCount = vertexCount,
             .stride      = description.stride,
             .attributes  = description.attributes,
             .indexCount  = static_cast<std::uint32_t>(description.indices.size()),
             .indexOffset = indexOffset});
}

TexturePtr
Renderer::createTexture(const TextureDescription& description)
{
    if (description.width == 0u || description.height == 0u)
    {
        throw std::invalid_argument{std::format("A texture needs a non-zero extent; got {}x{}.",
                                                description.width,
                                                description.height)};
    }

    constexpr std::uint64_t kBytesPerPixel = 4u;
    const std::uint64_t     size =
        std::uint64_t{description.width} * description.height * kBytesPerPixel;
    if (description.pixels.size() != size)
    {
        throw std::invalid_argument{std::format("A {}x{} texture takes {} bytes of pixels; got {}.",
                                                description.width,
                                                description.height,
                                                size,
                                                description.pixels.size())};
    }

    Graphics::Vulkan::BufferPtr staging = mVulkanDevice->createBuffer(
        {.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::ranges::copy(description.pixels, mapping.data);
    }

    Graphics::Vulkan::TexturePtr texture = mVulkanDevice->createTexture(
        mHeap, {description.width, description.height}, toVk(description.format));

    // After the texture exists, so that a frame which draws it finds its upload.
    {
        const std::scoped_lock lock{mUploadMutex};
        mPendingUploads.push_back({.staging = std::move(staging), .destination = texture});
    }

    return std::make_shared<const Texture>(Texture{.texture = std::move(texture)});
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
    if (description.textures.size() > gMaxTextures)
    {
        throw std::invalid_argument{std::format("A material takes at most {} textures; got {}.",
                                                gMaxTextures,
                                                description.textures.size())};
    }
    if (std::ranges::any_of(description.textures,
                            [](const TexturePtr& texture)
                            {
                                return !texture;
                            }))
    {
        throw std::invalid_argument{"A material texture is null."};
    }

    Graphics::Vulkan::ShaderProgramPtr program = mShaderProgramCache->get(description.shader);

    for (const Graphics::Vulkan::ShaderProgram::PushConstants::Range& range :
         program->pushConstants.ranges)
    {
        if (std::size_t{range.offset} + range.size > gPushDataSize)
        {
            throw std::invalid_argument{std::format(
                "Shader \"{}\" has a push-constant range [{}, {}) past the {} bytes of push data "
                "that the renderer pushes.",
                description.shader,
                range.offset,
                range.offset + range.size,
                gPushDataSize)};
        }
    }

    return std::make_shared<const Material>(
        Material{.program  = std::move(program),
                 .block    = mVulkanDevice->createUniformBlock(mHeap, description.parameters),
                 .textures = std::move(description.textures)});
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

    // Before the acquire: the slot's fences show that its semaphores are free to signal again. The
    // waits have no timeout, unlike the acquire.
    const auto frame = mFrameObjectManager->prepareNextFrame();

    const auto acquireResult =
        mSwapchain->acquireNextImage(gAcquireTimeout, frame->getAcquireSemaphore());
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

    // Advance the frame slot only when we will actually submit: skip/out-of-date must not burn a
    // fence-keyed release slot. Its pools were reset in prepareNextFrame().
    mFrameObjectManager->advance();
    auto pool = frame->getCommandBufferPoolForThread(gThreadId);

    auto recorder = pool->record();
    recorder.bindDescriptorHeap(mHeap);

    // Every upload that exists now, including those of resources that earlier dropped frames did
    // not record. They stay in the list until the graphics submit below returns.
    std::vector<PendingUpload> uploads;
    {
        const std::scoped_lock lock{mUploadMutex};
        uploads.assign(mPendingUploads.begin(), mPendingUploads.end());
    }

    // The copies go to the transfer queue; the graphics recorder takes the resources over before
    // the pass (nothing to record when the two queues share a family).
    const std::uint32_t graphicsFamily = mGraphicsQueue->familyIndex;
    const std::uint32_t transferFamily = mTransferQueue->familyIndex;

    std::optional<ExecutableCommandBuffer> transferCommands;
    if (!uploads.empty())
    {
        auto transferRecorder = frame->getTransferCommandBufferPool()->record();
        for (const PendingUpload& upload : uploads)
        {
            if (const auto* buffer = std::get_if<Graphics::Vulkan::BufferPtr>(&upload.destination))
            {
                transferRecorder.copyBuffer(upload.staging, *buffer, graphicsFamily);
                recorder.acquire(*buffer, transferFamily);
            }
            else
            {
                const auto& texture = std::get<Graphics::Vulkan::TexturePtr>(upload.destination);
                transferRecorder.copyBufferToTexture(upload.staging, texture, graphicsFamily);
                recorder.acquire(texture, transferFamily);
            }
        }
        transferCommands.emplace(transferRecorder.finish());
    }

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

                // The matrix, then the heap indices of the parameter block and the textures;
                // createMaterial checked that the shader's ranges end inside what is filled here.
                const Math::Matrix4x4f               mvp = viewProjection * draw.world;
                std::array<std::byte, gPushDataSize> pushData{};
                std::memcpy(pushData.data(), &mvp, sizeof(mvp));
                const std::uint32_t blockIndex = material.block->heapIndex();
                std::memcpy(
                    pushData.data() + gMaterialIndexOffset, &blockIndex, sizeof(blockIndex));

                pass.bindPipeline(mPipelines[i]);
                pass.useResource(material.block);
                for (std::size_t textureIndex = 0u; textureIndex < material.textures.size();
                     ++textureIndex)
                {
                    const std::shared_ptr<Graphics::Vulkan::Texture>& texture =
                        material.textures[textureIndex]->texture;
                    const std::uint32_t index = texture->heapIndex();
                    std::memcpy(pushData.data() + gTexturesOffset + (textureIndex * sizeof(index)),
                                &index,
                                sizeof(index));
                    pass.useResource(texture);
                }
                pass.pushData(0u, pushData);
                pass.bindVertexBuffers(0u, {{.buffer = draw.mesh->buffer, .offset = 0u}});
                if (draw.mesh->indexCount == 0u)
                {
                    pass.draw(draw.mesh->vertexCount, 1, 0, 0);
                }
                else
                {
                    pass.bindIndexBuffer(
                        draw.mesh->buffer, draw.mesh->indexOffset, vk::IndexType::eUint32);
                    pass.drawIndexed(draw.mesh->indexCount, 1, 0, 0, 0);
                }
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

    // The transfer submit goes first. Its value is committed once the submit returned, so a
    // submit that throws leaves the value and the pending uploads as they were. It waits for the
    // previous value too: after a frame whose graphics submit threw, the uploads are recorded
    // again while the first copy of them may still run.
    if (transferCommands)
    {
        const std::uint64_t next = mUploadValue + 1u;
        mTransferQueue->submit(
            *mFencePool,
            {},
            std::span{&*transferCommands, 1u},
            {},
            std::array{Queue::TimelineWait{.semaphore = mUploadTimeline,
                                           .value     = mUploadValue,
                                           .stage     = vk::PipelineStageFlagBits2::eAllCommands}},
            std::array{Queue::TimelineSignal{.semaphore = mUploadTimeline, .value = next}});
        mUploadValue = next;
    }

    // Every graphics submit waits for the last upload, not only one that uploads: a draw of a
    // resource that an earlier frame uploaded is ordered after that copy by this wait.
    mGraphicsQueue->submit(
        *mFencePool,
        std::array{
            Queue::SemaphoreWait{.semaphore = acquired->imageAvailableSemaphore(),
                                 .stage     = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
        std::span{&executable, 1u},
        std::array{frame->getRenderFinishedSemaphore()},
        std::array{Queue::TimelineWait{.semaphore = mUploadTimeline,
                                       .value     = mUploadValue,
                                       .stage     = vk::PipelineStageFlagBits2::eAllCommands}});

    // Only now: the transfer command buffer retains the uploads. Until the graphics submit returns
    // they stay pending, so a frame that fails before it records them again.
    {
        const std::scoped_lock lock{mUploadMutex};
        mPendingUploads.erase(mPendingUploads.begin(),
                              mPendingUploads.begin() +
                                  static_cast<std::ptrdiff_t>(uploads.size()));
    }

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
