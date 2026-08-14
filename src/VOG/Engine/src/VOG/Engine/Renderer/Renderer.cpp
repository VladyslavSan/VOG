#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Math/Math.hpp>
#include <VOG/Math/Projections.hpp>

#include <spdlog/spdlog.h>
#include <stddef.h>

#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

using namespace VOG::Math;

namespace VOG::Engine
{
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

Renderer::Renderer(const Common::SurfaceHandle& surfaceHandle,
                   const Common::JSONContainer& parameters)
    : mCurrentState{RenderJobState::eInactive}
    , mMaxFramesInFlight{std::clamp(parameters["frames_in_flight"].getOr<std::uint8_t>(1u),
                                    std::uint8_t{1},
                                    kMaxFramesInFlight)}
    , mVulkanInstance{Graphics::Vulkan::Instance::create({
          .appName    = parameters["app_name"].getOr<std::string>(""),
          .engineName = parameters["engine_name"].getOr<std::string>(""),
          .layers     = parameters["layers"].getArrayOfType<std::string>(),
          .extensions = parameters["extensions"].getArrayOfType<std::string>(),
      })}
    , mVulkanDevice{mVulkanInstance->makeDevice()}
    , mFencePool{std::make_shared<Graphics::Vulkan::FencePool>(mVulkanDevice)}
    , mShaderProgramCache{std::make_shared<Graphics::ShaderProgramCache>(
          mVulkanDevice,
          std::filesystem::path{parameters["shader_source_path"].getOr<std::string>("")})}
    , mSwapchain{mVulkanDevice->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = mMaxFramesInFlight,
          .surface        = surfaceHandle,
      })}
    , mFrameObjectManager{std::make_shared<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice,
          std::min<std::size_t>(mMaxFramesInFlight, mSwapchain->getImageCount()),
          1u)}
    , mScene{std::make_shared<Scene::Scene>()}
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
Renderer::render()
{
    using namespace VOG::Graphics::Vulkan;
    constexpr std::size_t threadId = 0u;

    struct VertexData
    {
        Vector2f position;
        Vector4f color;
    };

    std::shared_ptr<Graphics::Vulkan::Buffer> triangleBuffer;

    // clang-format off
    constexpr VertexData vertexData[] = {
      // Red bottom left square
      {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
      {{-1.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
      {{ 0.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
      {{ 0.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
      {{ 0.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
      {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
      // Greep top rigth square
      {{ 0.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
      {{ 0.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
      {{ 1.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
      {{ 0.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
      {{ 1.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
      {{ 1.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
    };
    // clang-format on

    constexpr std::size_t kVertexDataSize = std::size(vertexData) * sizeof(VertexData);

    triangleBuffer = mVulkanDevice->createBuffer(
        {.size = kVertexDataSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU});

    auto mapping = triangleBuffer->mapForWrite();
    std::memcpy(mapping.data, vertexData, kVertexDataSize);

    constexpr std::chrono::milliseconds kZeroExtentBackoff{16};

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

    // Advance the frame slot only when we will actually submit — skip/out-of-date must not
    // burn a fence-keyed release slot.
    auto frame = mFrameObjectManager->acquireNextFrame();
    auto pool  = frame->getCommandBufferPoolForThread(threadId);

    auto commandBuffer = pool->get(vk::CommandBufferLevel::ePrimary);
    commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    commandBuffer->bindVertexBuffers(0u, {{.buffer = triangleBuffer, .offset = 0u}});

    Graphics::Vulkan::CommandBufferRecorder recorder{*mVulkanDevice, **commandBuffer};

    recorder.setBarriers({},
                         {},
                         {{.srcStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
                           .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                           .oldLayout    = vk::ImageLayout::eUndefined,
                           .newLayout    = vk::ImageLayout::eColorAttachmentOptimal,
                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .image               = mSwapchain->getImage(),
                           .subresourceRange    = {
                               .aspectMask     = vk::ImageAspectFlagBits::eColor,
                               .baseMipLevel   = 0,
                               .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                               .baseArrayLayer = 0,
                               .layerCount     = VK_REMAINING_ARRAY_LAYERS,
                           }}});

    const double time = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::high_resolution_clock::now() -
                                                std::chrono::high_resolution_clock::time_point{})
                                                .count());

    const float red  = static_cast<float>((std::sin(time / (2 * std::numbers::pi) / 100) + 1) / 2);
    const float blue = static_cast<float>((std::cos(time / (2 * std::numbers::pi) / 100) + 1) / 2);

    const std::array clearColor = {red * 0.2f, 0.0f, blue * 0.2f, 1.0f};

    auto renderPass =
        mVulkanDevice->createRenderPass({{.format        = mSwapchain->getFormat(),
                                          .loadOp        = vk::AttachmentLoadOp::eClear,
                                          .storeOp       = vk::AttachmentStoreOp::eStore,
                                          .initialLayout = vk::ImageLayout::eUndefined,
                                          .finalLayout = vk::ImageLayout::eColorAttachmentOptimal}},
                                        {});

    auto framebuffer = mVulkanDevice->createFramebuffer(renderPass, {mSwapchain}, nullptr);

    {
        recorder.beginRenderPass(renderPass, framebuffer, {vk::ClearColorValue{clearColor}});

        auto program = mShaderProgramCache->get("WorldSpace");

        auto pipeline = mVulkanDevice->createGraphicsPipeline(GraphicsPipeline::ParametersLegacy{
            .cache          = nullptr,
            .shading        = program,
            .vertexLayout   = {.bindingDescription =
                                   {
                                       {.binding   = 0u,
                                        .stride    = sizeof(VertexData),
                                        .inputRate = vk::VertexInputRate::eVertex},
                                   },
                               .attributeDescription = {{.location = 0u,
                                                         .binding  = 0u,
                                                         .format   = vk::Format::eR32G32Sfloat,
                                                         .offset   = 0u},
                                                        {.location = 1u,
                                                         .binding  = 0u,
                                                         .format   = vk::Format::eR32G32B32A32Sfloat,
                                                         .offset   = offsetof(VertexData, color)}}},
            .rasterizer     = {.cullMode = CullMode::eNone},
            .viewportState  = {.viewportCount = 1u, .scissorCount = 1u},
            .depthStencil   = {.depthTestEnable = 0u},
            .blending       = {.attachments = {{.blendEnable = 0u,
                                                .colorWriteMask =
                                                    ColorComponent::eR | ColorComponent::eG |
                                                    ColorComponent::eB | ColorComponent::eA}}},
            .multisample    = {},
            .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
            .pipelineLayout = *program->pipelineLayout,
            .renderPass     = *renderPass,
            .subpass        = 0u});
        recorder.bindPipeline(pipeline);

        const auto extent = mSwapchain->getExtent();
        recorder->setViewport(0,
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
        recorder->setScissor(0,
                             {
                                 {
                                     .offset = {.x = 0, .y = 0},
                                     .extent = {.width = extent.width, .height = extent.height},
                                 },
                             });

        const float timeFactor = static_cast<float>(std::sin(time / (2 * std::numbers::pi) / 100));

        const Vector3f objectPosition = {0.0, 0.0, 20.0};
        const float    factor         = 300.0;
        const auto model = glm::translate(objectPosition) *
                           glm::scale(Vector3f{1.0 * factor, 1.0 * factor, 1.0}) *
                           glm::rotate(glm::radians(timeFactor * 90.0f), Vector3f{0.0, 0.0, 1.0});

        const Vector3f cameraPosition  = {0.0, 0.0, 0.0};
        const Vector3f cameraDirection = glm::normalize(objectPosition - cameraPosition);
        const Vector3f cameraUp        = {0.0, 1.0, 0.0};

        const Matrix4x4f cameraMatrix =
            Math::calculateCameraMatrix(cameraPosition, cameraDirection, cameraUp);

        const Matrix4x4f viewMatrix = glm::inverse(cameraMatrix);
        const Matrix4x4f projection = Math::orthographicProjection(
            {0.0, 0.0},
            {static_cast<float>(extent.width), static_cast<float>(extent.height)},
            1.0,
            200.0);

        const Matrix4x4f mvp = projection * viewMatrix * model;

        recorder->pushConstants<std::byte>(
            *program->pipelineLayout,
            vk::ShaderStageFlagBits::eVertex,
            0u,
            vk::ArrayProxy<const std::byte>{sizeof(mvp), reinterpret_cast<const std::byte*>(&mvp)});

        recorder->draw(std::size(vertexData), 1, 0, 0);

        recorder.endRenderPass();
    }

    recorder.setBarriers({},
                         {},
                         {{.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                           .dstStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
                           .oldLayout    = vk::ImageLayout::eColorAttachmentOptimal,
                           .newLayout    = vk::ImageLayout::ePresentSrcKHR,
                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .image               = mSwapchain->getImage(),
                           .subresourceRange    = {
                               .aspectMask     = vk::ImageAspectFlagBits::eColor,
                               .baseMipLevel   = 0,
                               .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                               .baseArrayLayer = 0,
                               .layerCount     = VK_REMAINING_ARRAY_LAYERS,
                           }}});

    commandBuffer->end();

    mVulkanDevice->graphicsQueue.submit(
        mFencePool,
        std::array{**acquireResult.imageAvailableSemaphore},
        std::array{vk::PipelineStageFlags{vk::PipelineStageFlagBits::eColorAttachmentOutput}},
        std::array{std::move(commandBuffer)},
        std::array{*frame->getRenderFinishedSemaphore()});

    if (mSwapchain->present(*frame->getRenderFinishedSemaphore()) ==
        Swapchain::PresentStatus::eOutOfDate)
    {
        if (!mSwapchain->recreate())
        {
            std::this_thread::sleep_for(kZeroExtentBackoff);
        }
    }
}

const std::shared_ptr<Scene::Scene>&
Renderer::getScene() const
{
    return mScene;
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
