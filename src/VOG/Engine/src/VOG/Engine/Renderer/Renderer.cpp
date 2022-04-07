#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/Math/Math.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>
#include <VOG/Graphics/ResourceManager.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>

#include <stddef.h>

#include <chrono>
#include <cmath>
#include <numbers>

namespace VOG::Engine
{
Renderer::~Renderer() { requestRenderChangeState(RenderJobState::eInactive); }

Renderer::Renderer(const Common::JSONContainer& parameters)
    : mCurrentState{RenderJobState::eInactive}
    , mMaxFramesInFlight{std::clamp(parameters["frames_in_flight"].getOr<std::uint8_t>(1u),
                                    std::uint8_t{1},
                                    MaxFramesInFlight)}
    , mRenderFrame{0}
    , mGraphicsProvider{std::make_shared<Graphics::GraphicsProvider>(parameters)}
    , mResourceManager{std::make_shared<Graphics::ResourceManager>(mGraphicsProvider,
                                                                   parameters["resource_manager"])}
    , mSwapchain{mResourceManager->createRenderSurface(parameters["surface"])}
    , mFrameObjectManager{std::make_shared<Graphics::Frame::FrameObjectManager>(
          mGraphicsProvider, mMaxFramesInFlight, 1u)}
    , mScene{std::make_shared<Scene::Scene>()}
{
}

void
Renderer::render()
{
    constexpr std::size_t threadId = 0u;

    struct VertexData
    {
        Common::Math::Vec2f position;
        Common::Math::Vec4f color;
    };
    std::shared_ptr<Graphics::Vulkan::Buffer> triangleBuffer;
    {
        constexpr VertexData vertexData[] = {{{0.0, -0.5}, {1.0, 0.0, 0.0, 1.0}},
                                             {{0.5, 0.5}, {0.0, 1.0, 0.0, 1.0}},
                                             {{-0.5, 0.5}, {0.0, 0.0, 1.0, 1.0}}};

        constexpr std::size_t kVertexDataSize = std::size(vertexData) * sizeof(VertexData);

        triangleBuffer = mGraphicsProvider->getMemoryAllocator()->makeBuffer(
            {.size = kVertexDataSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
            {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU});

        // clang-format on
        auto mapping = triangleBuffer->mapForWrite();
        std::memcpy(mapping.data, vertexData, kVertexDataSize);
    }

    if (mSwapchain->acquireNextImage() != vk::Result::eSuccess)
    {
        throw std::runtime_error{"Could not acquire image"};
    }

    const std::size_t frameInFlightIndex = getFrameInFlightIndex();

    auto& frame = mFrameObjectManager->getFrameObjects(frameInFlightIndex);
    frame.onFrameStart();

    auto& thread = frame.getThreadObjects(threadId);

    auto commandBuffer = thread.getCommandBuffer(vk::CommandBufferLevel::ePrimary);
    commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    commandBuffer->bindVertexBuffers(0, {**triangleBuffer}, {0u});
    commandBuffer->addBoundResource(triangleBuffer);

    Graphics::Vulkan::CommandBufferRecorder recorder{mGraphicsProvider->getDevice(),
                                                     **commandBuffer};

    recorder.setBarriers({},
                         {{.dstStageMask        = vk::PipelineStageFlagBits2::eVertexShader,
                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .buffer              = **triangleBuffer,
                           .offset              = 0u,
                           .size                = VK_WHOLE_SIZE}},
                         {{.srcStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe,
                           .dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                           .oldLayout    = vk::ImageLayout::eUndefined,
                           .newLayout    = vk::ImageLayout::eColorAttachmentOptimal,
                           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                           .image               = mSwapchain->getImage(),
                           .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                                   .baseMipLevel   = 0,
                                                   .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                                   .baseArrayLayer = 0,
                                                   .layerCount     = VK_REMAINING_ARRAY_LAYERS}}});

    const float time = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::high_resolution_clock::now() -
                           std::chrono::high_resolution_clock::time_point{})
                           .count();

    const float red  = (std::sin(time / (2 * std::numbers::pi) / 100) + 1) / 2;
    const float blue = (std::cos(time / (2 * std::numbers::pi) / 100) + 1) / 2;

    const std::array clearColor = {red, 1.0f, blue, 1.0f};

    auto renderPass = Graphics::Vulkan::RenderPass::create(
        mGraphicsProvider->getDevice(),
        {{.format        = mSwapchain->getFormat(),
          .loadOp        = vk::AttachmentLoadOp::eClear,
          .storeOp       = vk::AttachmentStoreOp::eStore,
          .initialLayout = vk::ImageLayout::eUndefined,
          .finalLayout   = vk::ImageLayout::eColorAttachmentOptimal}},
        {});

    auto framebuffer = Graphics::Vulkan::Framebuffer::create(
        mGraphicsProvider->getDevice(), {mSwapchain}, *renderPass);

    {
        recorder.beginRenderPass(renderPass, framebuffer, {vk::ClearColorValue{clearColor}});

        vk::raii::PipelineLayout layout{mGraphicsProvider->getDevice(),
                                        vk::PipelineLayoutCreateInfo{}};

        auto program = mResourceManager->createShaderProgram("ScreenSpacePositionColor");

        vk::PipelineColorBlendAttachmentState blendState{
            .blendEnable    = 0u,
            .colorWriteMask = {vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}};

        auto pipeline = Graphics::Vulkan::GraphicsPipeline::create(
            mGraphicsProvider->getDevice(),
            nullptr,
            *program,
            {.bindingDescription =
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
            {.cullMode = vk::CullModeFlagBits::eNone},
            {.viewportCount = 1u, .scissorCount = 1u},
            vk::PipelineDepthStencilStateCreateInfo{},
            vk::PipelineColorBlendStateCreateInfo{.attachmentCount = 1,
                                                  .pAttachments    = &blendState},
            vk::PipelineMultisampleStateCreateInfo{},
            {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
            *layout,
            **renderPass,
            0u);
        recorder.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        const auto extent = mSwapchain->getExtent();
        recorder->setViewport(0,
                              {{.x      = 0.0f,
                                .y      = 0.0f,
                                .width  = static_cast<float>(extent.width),
                                .height = static_cast<float>(extent.height)}});
        recorder->setScissor(0,
                             {{.offset = {.x = 0, .y = 0},
                               .extent = {.width = extent.width, .height = extent.height}}});
        recorder->draw(3, 1, 0, 0);

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
                           .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                                   .baseMipLevel   = 0,
                                                   .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                                   .baseArrayLayer = 0,
                                                   .layerCount     = VK_REMAINING_ARRAY_LAYERS}}});

    commandBuffer->end();

    {
        auto&      timelineSemaphore = frame.getTimelineSemaphore();
        const auto nextValue         = timelineSemaphore.getNextWaitValue();
        frame.submit(std::move(commandBuffer),
                     {},
                     {.semaphore = &timelineSemaphore, .value = nextValue},
                     frame.getFramePresentWaitSemaphore());
    }

    auto waitSemaphores = {*frame.getFramePresentWaitSemaphore()};
    mSwapchain->present(waitSemaphores);

    ++mRenderFrame;
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
    if (newState == mCurrentState.load())
    {
        return false;
    }

    switch (newState)
    {
    case RenderJobState::eActive:
    {
        mRenderThread = ThreadType{&Renderer::renderThreadMain, weak_from_this()};
        break;
    }
    case RenderJobState::eInactive:
    {
        mRenderThread = ThreadType{};
    }
    default:
        break;
    }

    mCurrentState.store(newState);

    return true;
}

std::size_t
Renderer::getFrameInFlightIndex() const
{
    return mRenderFrame.load() % mMaxFramesInFlight;
}

void
Renderer::renderThreadMain(std::weak_ptr<Renderer> renderer)
{
    while (true)
    {
        if (boost::this_thread::interruption_requested())
        {
            break;
        }

        auto locked = renderer.lock();
        locked->render();
    }
}
} // namespace VOG::Engine
