#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/Math/Math.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
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
                                    kMaxFramesInFlight)}
    , mVulkanInstance{Graphics::Vulkan::Instance::create({
          .appName    = parameters["app_name"].getOr<std::string>(""),
          .engineName = parameters["engine_name"].getOr<std::string>(""),
          .layers     = parameters["layers"].getArrayOfType<std::string>(),
          .extensions = parameters["extensions"].getArrayOfType<std::string>(),
      })}
    , mVulkanDevice{mVulkanInstance->makeDevice()}
    , mShaderProgramCache{std::make_shared<Graphics::ShaderProgramCache>(
          mVulkanDevice,
          std::filesystem::path{parameters["shader_source_path"].getOr<std::string>("")})}
    , mSwapchain{std::make_shared<Graphics::Vulkan::Swapchain>(mVulkanDevice,
                                                               parameters["surface"])}
    , mFrameObjectManager{std::make_shared<Graphics::Frame::FrameObjectManager>(
          mVulkanDevice, mMaxFramesInFlight, 1u)}
    , mScene{std::make_shared<Scene::Scene>()}
{
}

void
Renderer::render()
{
    using namespace VOG::Graphics::Vulkan;
    constexpr std::size_t threadId = 0u;

    struct VertexData
    {
        Common::Math::Vec2f position;
        Common::Math::Vec4f color;
    };
    std::shared_ptr<Graphics::Vulkan::Buffer> triangleBuffer;
    {
        // clang-format off
        constexpr VertexData vertexData[] = {{{ 0.0, -0.5}, {1.0, 0.0, 0.0, 1.0}},
                                             {{ 0.5,  0.5}, {0.0, 1.0, 0.0, 1.0}},
                                             {{-0.5,  0.5}, {0.0, 0.0, 1.0, 1.0}},
                                             {{ 0.0, -0.5}, {1.0, 0.0, 0.0, 1.0}},
                                             {{ 0.5,  0.5}, {0.0, 1.0, 0.0, 1.0}},
                                             {{-0.5,  0.5}, {0.0, 0.0, 1.0, 1.0}}};
        // clang-format on

        constexpr std::size_t kVertexDataSize = std::size(vertexData) * sizeof(VertexData);

        triangleBuffer = mVulkanDevice->memoryAllocator->makeBuffer(
            {.size = kVertexDataSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
            {.usage = VMA_MEMORY_USAGE_CPU_TO_GPU});

        auto mapping = triangleBuffer->mapForWrite();
        std::memcpy(mapping.data, vertexData, kVertexDataSize);
    }

    if (mSwapchain->acquireNextImage() != vk::Result::eSuccess)
    {
        throw std::runtime_error{"Could not acquire image"};
    }

    auto& frame = mFrameObjectManager->acquireNextFrame();
    auto& pool  = frame.getCommandBufferPoolForThread(threadId);

    auto commandBuffer = pool.get(vk::CommandBufferLevel::ePrimary);
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
        *mVulkanDevice,
        {{.format        = mSwapchain->getFormat(),
          .loadOp        = vk::AttachmentLoadOp::eClear,
          .storeOp       = vk::AttachmentStoreOp::eStore,
          .initialLayout = vk::ImageLayout::eUndefined,
          .finalLayout   = vk::ImageLayout::eColorAttachmentOptimal}},
        {});

    auto framebuffer =
        Graphics::Vulkan::Framebuffer::create(*mVulkanDevice, *renderPass, {mSwapchain}, nullptr);

    {
        recorder.beginRenderPass(renderPass, framebuffer, {vk::ClearColorValue{clearColor}});

        vk::raii::PipelineLayout layout{*mVulkanDevice, vk::PipelineLayoutCreateInfo{}};

        auto program = mShaderProgramCache->get("ScreenSpacePositionColor");

        auto pipeline = std::make_shared<GraphicsPipeline>(GraphicsPipeline::CreateInfo{
            .device         = mVulkanDevice,
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
            .depthStencil   = {},
            .blending       = {.attachments = {{.blendEnable = 0u,
                                                .colorWriteMask =
                                                    ColorComponent::eR | ColorComponent::eG |
                                                    ColorComponent::eB | ColorComponent::eA}}},
            .multisample    = {},
            .dynamicStates  = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
            .pipelineLayout = *layout,
            .renderPass     = *renderPass,
            .subpass        = 0u});
        recorder.bindPipeline(pipeline);

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
        auto& timelineSemaphore = frame.getTimelineSemaphore();
        timelineSemaphore.incrementCounter();
        commandBuffer.submit({}, timelineSemaphore, frame.getFramePresentWaitSemaphore());
    }

    auto waitSemaphores = {*frame.getFramePresentWaitSemaphore()};
    mSwapchain->present(waitSemaphores);
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

void
Renderer::renderThreadMain(std::weak_ptr<Renderer> renderer)
{
    while (!boost::this_thread::interruption_requested())
    {
        auto locked = renderer.lock();
        if (!locked)
        {
            break;
        }
        locked->render();
    }
}
} // namespace VOG::Engine
