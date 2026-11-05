#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/Assert.hpp>
#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/DrawRecording.hpp"
#include "VOG/Engine/Renderer/FrameBlocks.hpp"
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineCache.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
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
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

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

/**
 * Images and heap buffers the descriptor heap holds at once.
 * Fixed, because growing means binding another heap; make it grow when a scene runs out.
 */
constexpr std::uint32_t gDescriptorHeapSlots = 4096u;

/** A sampler of the sampler heap: one filter for magnification and minification, repeating. */
constexpr vk::SamplerCreateInfo
makeSampler(vk::Filter filter)
{
    return {.magFilter    = filter,
            .minFilter    = filter,
            .mipmapMode   = vk::SamplerMipmapMode::eLinear,
            .addressModeU = vk::SamplerAddressMode::eRepeat,
            .addressModeV = vk::SamplerAddressMode::eRepeat,
            .addressModeW = vk::SamplerAddressMode::eRepeat,
            .maxLod       = vk::LodClampNone};
}

/** The samplers of the sampler heap; the index of a sampler is the value of its Filter, which
 *  the include VOG/Draw.glsl documents: 0 linear, 1 nearest. */
constexpr std::array gSamplers{makeSampler(vk::Filter::eLinear), makeSampler(vk::Filter::eNearest)};
static_assert(std::to_underlying(Filter::eLinear) == 0u &&
              std::to_underlying(Filter::eNearest) == 1u);

} // namespace

Renderer::Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config)
    : mFramesInFlight{std::clamp(config.framesInFlight, std::uint8_t{1}, kMaxFramesInFlight)}
    , mVulkanInstance{Graphics::Vulkan::Instance::create({
          .appName     = config.appName,
          .engineName  = config.engineName,
          .layers      = config.layers,
          .extensions  = config.extensions,
          .messageSink = config.onValidationMessage,
      })}
    , mVulkanDevice{mVulkanInstance->makeDevice()}
    , mGraphicsQueue{mVulkanDevice->getGraphicsQueue()}
    , mTransferQueue{mVulkanDevice->getTransferQueue()}
    , mFencePool{Graphics::Vulkan::FencePool::create(mVulkanDevice)}
    , mShaderLibrary{std::make_unique<ShaderLibrary>(mVulkanDevice, config.shaderSourcePath)}
    , mHeap{mVulkanDevice->createDescriptorHeap(gDescriptorHeapSlots, gSamplers)}
    , mSwapchain{mVulkanDevice->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = mFramesInFlight,
          .surface        = surfaceHandle,
          .extent         = toVk(config.extent),
      })}
    , mFrameObjectManager{std::make_unique<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice, frameSlotCount(mFramesInFlight, mSwapchain->getImageCount()), 1u)}
    , mFrameBlocks{std::make_unique<FrameBlocks>(mVulkanDevice, mHeap)}
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

Mesh
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
    if (description.attributes.size() > Graphics::Vulkan::Limits::gMaxNumVertexInputs)
    {
        throw std::invalid_argument{std::format("A mesh takes at most {} attributes; got {}.",
                                                Graphics::Vulkan::Limits::gMaxNumVertexInputs,
                                                description.attributes.size())};
    }
    const vk::PhysicalDeviceLimits& limits = mVulkanDevice->limits;

    constexpr std::uint32_t kComponentAlignment = 4u;
    if (description.stride % kComponentAlignment != 0u ||
        description.stride > limits.maxVertexInputBindingStride)
    {
        throw std::invalid_argument{
            std::format("A mesh stride must be a multiple of {} and at most {}; got {}.",
                        kComponentAlignment,
                        limits.maxVertexInputBindingStride,
                        description.stride)};
    }
    for (const VertexAttribute& attribute : description.attributes)
    {
        if (attribute.offset % kComponentAlignment != 0u ||
            attribute.offset > limits.maxVertexInputAttributeOffset)
        {
            throw std::invalid_argument{std::format(
                "A vertex attribute offset must be a multiple of {} and at most {}; got {}.",
                kComponentAlignment,
                limits.maxVertexInputAttributeOffset,
                attribute.offset)};
        }
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

    // The indices follow the vertices. The stride is a multiple of 4, so they are aligned to the
    // size of an index.
    const std::size_t indexOffset = description.vertices.size_bytes();

    const std::size_t size = indexOffset + description.indices.size_bytes();

    // The staging buffer takes the bytes on this thread; the render thread copies them into the
    // buffer of the mesh, preferably device-local, which only the GPU reads.
    Graphics::Vulkan::BufferPtr staging = mVulkanDevice->createBuffer(
        {.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::ranges::copy(description.vertices, mapping.bytes().begin());
        std::ranges::copy(std::as_bytes(description.indices),
                          mapping.bytes().subspan(indexOffset).begin());
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

    return Mesh{std::make_shared<const Detail::MeshData>(
        Detail::MeshData{.buffer      = std::move(buffer),
                         .vertexCount = vertexCount,
                         .stride      = description.stride,
                         .attributes  = description.attributes,
                         .indexCount  = static_cast<std::uint32_t>(description.indices.size()),
                         .indexOffset = indexOffset})};
}

Texture
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

    // The texture first: it validates the extent against the device, and an extent that no image
    // can have must not reach the (possibly huge) staging allocation.
    Graphics::Vulkan::TexturePtr texture = mVulkanDevice->createTexture(
        mHeap, {description.width, description.height}, toVk(description.format));

    Graphics::Vulkan::BufferPtr staging = mVulkanDevice->createBuffer(
        {.size = size, .usage = vk::BufferUsageFlagBits::eTransferSrc},
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = staging->mapForWrite();
        std::ranges::copy(description.pixels, mapping.bytes().begin());
    }

    // After the texture exists, so that a frame which draws it finds its upload.
    {
        const std::scoped_lock lock{mUploadMutex};
        mPendingUploads.push_back({.staging = std::move(staging), .destination = texture});
    }

    return Texture{std::make_shared<const Detail::TextureData>(
        Detail::TextureData{.texture = std::move(texture), .filter = description.filter})};
}

Shader
Renderer::getShader(std::string_view name) const
{
    return Shader{mShaderLibrary->get(name)};
}

Material
Renderer::createMaterial(MaterialDescription description)
{
    const std::string context = description.name.empty()
                                  ? std::string{}
                                  : std::format("Material \"{}\": ", description.name);

    if (!description.shader)
    {
        throw std::invalid_argument{std::format(
            "{}A material needs a shader; get it from Renderer::getShader().", context)};
    }
    const std::shared_ptr<const Detail::ShaderInfo>& shader = description.shader.mData;

    const std::vector<std::byte> bytes =
        packMaterialBlock(*shader, description.parameters, context);

    // The parameters are written once, here: nothing writes the block after the material exists.
    Graphics::Vulkan::HeapBufferPtr block = mVulkanDevice->createHeapBuffer(mHeap, bytes.size());
    {
        auto mapping = block->mapForWrite();
        std::ranges::copy(bytes, mapping.bytes().begin());
    }

    std::vector<Texture> textures;
    for (const MaterialParameter& parameter : description.parameters)
    {
        if (const auto* texture = std::get_if<Texture>(&parameter.value))
        {
            textures.push_back(*texture);
        }
    }

    return Material{std::make_shared<const Detail::MaterialData>(
        Detail::MaterialData{.shader     = shader,
                             .block      = std::move(block),
                             .textures   = std::move(textures),
                             .parameters = std::move(description.parameters),
                             .state      = description.state})};
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
    FramePacket packet{.frame = frameContext};
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
            mPipelineCache->get(*draw.mesh.mData, *draw.material.mData, mSwapchain->getFormat()));
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
Renderer::renderFrame(const FramePacket& packet)
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

    const vk::Extent2D extent = acquired->getExtent2D();

    // The slot's fences were waited in prepareNextFrame(), so no draw of an earlier frame reads
    // this buffer any more.
    const Graphics::Vulkan::HeapBufferPtr frameBlock = mFrameBlocks->of(frame);
    DrawRecording::writeFrameBlock(*frameBlock, packet, extent);

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

            DrawRecording::recordDraws(pass, packet, mPipelines, frameBlock, extent);
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
            {.commandBuffers = std::span{&*transferCommands, 1u},
             .timelineWaits =
                 std::array{Queue::TimelineWait{.semaphore = mUploadTimeline,
                                                .value     = mUploadValue,
                                                .stage = vk::PipelineStageFlagBits2::eAllCommands}},
             .timelineSignals =
                 std::array{Queue::TimelineSignal{.semaphore = mUploadTimeline, .value = next}}});
        mUploadValue = next;
    }

    // Every graphics submit waits for the last upload, not only one that uploads: a draw of a
    // resource that an earlier frame uploaded is ordered after that copy by this wait.
    mGraphicsQueue->submit(*mFencePool,
                           {.waits          = std::array{Queue::SemaphoreWait{
                                .semaphore = acquired->imageAvailableSemaphore(),
                                .stage     = vk::PipelineStageFlagBits2::eColorAttachmentOutput}},
                            .commandBuffers = std::span{&executable, 1u},
                            .signals        = std::array{frame->getRenderFinishedSemaphore()},
                            .timelineWaits  = std::array{Queue::TimelineWait{
                                .semaphore = mUploadTimeline,
                                .value     = mUploadValue,
                                .stage     = vk::PipelineStageFlagBits2::eAllCommands}}});

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
                                 renderFrame(*packet);
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
