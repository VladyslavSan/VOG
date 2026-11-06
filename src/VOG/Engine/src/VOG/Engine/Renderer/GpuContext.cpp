#include "VOG/Engine/Renderer/GpuContext.hpp"

#include <VOG/Engine/Renderer/Texture.hpp>
#include <VOG/Graphics/Frame/FrameObjectManager.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/Queue.hpp>
#include <VOG/Graphics/Vulkan/TimelineSemaphore.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace VOG::Engine
{
namespace
{
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

constexpr std::size_t gThreadCount = 1u;

/** The layers of the instance: the validation layer if the configuration asks for it. */
std::vector<std::string>
layersOf(const Renderer::Config& config)
{
    return config.validation ? std::vector<std::string>{"VK_LAYER_KHRONOS_validation"}
                             : std::vector<std::string>{};
}

/** The instance extensions that the surface needs and that the instance does not add itself: the
 *  platforms with a window are known to the instance, a headless surface is not. */
std::vector<std::string>
extensionsOf(const Common::SurfaceHandle& surfaceHandle)
{
    return surfaceHandle.platform == Common::SurfaceHandle::Platform::eHeadless
             ? std::vector<std::string>{"VK_EXT_headless_surface"}
             : std::vector<std::string>{};
}

/**
 * The format of the depth target. The spec requires a device to render into at least one of
 * X8_D24_UNORM_PACK32 and D32_SFLOAT as a depth attachment (not both); D32_SFLOAT is the better
 * one, and the other is the fallback. Neither has a stencil aspect, which the recorder needs.
 */
vk::Format
chooseDepthFormat(const Graphics::Vulkan::Device& device)
{
    for (const vk::Format format : {vk::Format::eD32Sfloat, vk::Format::eX8D24UnormPack32})
    {
        const vk::FormatProperties properties =
            device.raiiPhysicalDevice().getFormatProperties(format);
        if (properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)
        {
            return format;
        }
    }

    throw std::runtime_error{
        "The device renders into neither D32_SFLOAT nor X8_D24_UNORM_PACK32 as a depth "
        "attachment, which the spec requires of one of them."};
}
} // namespace

GpuContext::GpuContext(const Common::SurfaceHandle& surfaceHandle, const Renderer::Config& config)
    : framesInFlight{
          std::clamp(config.framesInFlight, std::uint8_t{1}, Renderer::kMaxFramesInFlight)}
    , instance{Graphics::Vulkan::Instance::create({
          .appName     = config.appName,
          .engineName  = config.engineName,
          .layers      = layersOf(config),
          .extensions  = extensionsOf(surfaceHandle),
          .messageSink = config.onValidationMessage,
      })}
    , device{instance->makeDevice()}
    , graphicsQueue{device->getGraphicsQueue()}
    , fencePool{Graphics::Vulkan::FencePool::create(device)}
    , heap{device->createDescriptorHeap(config.resourceSlots, gSamplers)}
    , swapchain{device->createSwapchain(Graphics::Vulkan::Swapchain::SwapchainParameters{
          .framesInFlight = framesInFlight,
          .surface        = surfaceHandle,
          .extent         = toVk(config.extent),
      })}
    , depthFormat{chooseDepthFormat(*device)}
    , frameSlots{frameSlotCount(framesInFlight, swapchain->getImageCount())}
    , frames{std::make_unique<Graphics::Frame::FrameObjectManager>(
          device,
          Graphics::Frame::FrameObjectManager::Counts{.frames  = frameSlots,
                                                      .threads = gThreadCount})}
    , frameBlocks{device, heap, frameSlots}
    , uploadTimeline{device->createTimelineSemaphore()}
{
}

GpuContext::~GpuContext() = default;

std::uint8_t
GpuContext::frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount)
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
} // namespace VOG::Engine
