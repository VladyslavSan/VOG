#include "VOG/Engine/Renderer/Renderer.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>
#include <VOG/Graphics/ResourceManager.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/RenderCommandRecorder.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
#include <boost/thread/thread.hpp>

#include <chrono>
#include <cmath>
#include <numbers>

namespace VOG::Engine
{
Renderer::~Renderer() {}

Renderer::Renderer(const Common::JSONContainer& parameters)
    : mRenderThread{}
    , mCurrentState{RenderJobState::Inactive}
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
Renderer::Render()
{
    constexpr std::size_t threadId = 0u;

    if (mSwapchain->acquireNextImage() != vk::Result::eSuccess)
        throw std::runtime_error{"Could not acquire image"};

    const std::size_t frameInFlightIndex = getFrameInFlightIndex();

    auto& frame = mFrameObjectManager->getFrameObjects(frameInFlightIndex);
    frame.onFrameStart();

    auto& thread = frame.getThreadObjects(threadId);

    auto commandBuffer = thread.getCommandBuffer(vk::CommandBufferLevel::ePrimary);
    commandBuffer->begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    Graphics::Vulkan::RenderCommandRecorder recorder{mGraphicsProvider->getDevice(),
                                                     **commandBuffer};

    {
        vk::ImageMemoryBarrier2KHR barier{
            .srcStageMask        = vk::PipelineStageFlagBits2KHR::eBottomOfPipe,
            .dstStageMask        = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput,
            .oldLayout           = vk::ImageLayout::eUndefined,
            .newLayout           = vk::ImageLayout::eColorAttachmentOptimal,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = mSwapchain->getImage(),
            .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                    .baseMipLevel   = 0,
                                    .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                    .baseArrayLayer = 0,
                                    .layerCount     = VK_REMAINING_ARRAY_LAYERS}};

        vk::DependencyInfoKHR info{};
        const auto            imageBariers = {barier};
        info.setImageMemoryBarriers(imageBariers);
        commandBuffer->pipelineBarrier2KHR(info);
    }

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

        auto program = mResourceManager->createShaderProgram("BasicTriangle");

        vk::PipelineColorBlendAttachmentState blendState{
            .blendEnable    = false,
            .colorWriteMask = {vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                               vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}};

        auto pipeline = Graphics::Vulkan::GraphicsPipeline::create(
            mGraphicsProvider->getDevice(),
            nullptr,
            *program,
            {},
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
        recorder->addBoundResource(pipeline);
        recorder->bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline);

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

    {
        vk::ImageMemoryBarrier2KHR barier{
            .srcStageMask        = vk::PipelineStageFlagBits2KHR::eColorAttachmentOutput,
            .dstStageMask        = vk::PipelineStageFlagBits2KHR::eTopOfPipe,
            .oldLayout           = vk::ImageLayout::eColorAttachmentOptimal,
            .newLayout           = vk::ImageLayout::ePresentSrcKHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image               = mSwapchain->getImage(),
            .subresourceRange    = {.aspectMask     = vk::ImageAspectFlagBits::eColor,
                                    .baseMipLevel   = 0,
                                    .levelCount     = VK_REMAINING_ARRAY_LAYERS,
                                    .baseArrayLayer = 0,
                                    .layerCount     = VK_REMAINING_ARRAY_LAYERS}};

        vk::DependencyInfoKHR info{};
        const auto            imageBariers = {barier};
        info.setImageMemoryBarriers(imageBariers);
        commandBuffer->pipelineBarrier2KHR(info);
    }

    commandBuffer->end();

    {
        auto&      timelineSemaphore = frame.getTimelineSemaphore();
        const auto nextValue         = timelineSemaphore.getNextWaitValue();
        frame.submit(std::move(commandBuffer),
                     {},
                     {.semapore = &timelineSemaphore, .value = nextValue},
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
        return false;

    switch (newState)
    {
    case RenderJobState::Active:
    {
        mRenderThread = ThreadType{&Renderer::renderThreadMain, weak_from_this()};
        break;
    }
    case RenderJobState::Inactive:
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
        locked->Render();
    }
}
} // namespace VOG::Engine
