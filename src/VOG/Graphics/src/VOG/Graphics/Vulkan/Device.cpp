#include "VOG/Graphics/Vulkan/Device.hpp"

#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/UniformBlock.hpp>

#include <algorithm>
#include <format>
#include <utility>

namespace VOG::Graphics::Vulkan
{
namespace
{
const char* gPortabilityExtensionName = "VK_KHR_portability_subset";

PhysicalDevice::QueueInfos
findGraphicsTransferQueues(const vk::raii::PhysicalDevice& device)
{
    constexpr vk::QueueFlags kGraphicsFamily = vk::QueueFlagBits::eGraphics;
    constexpr vk::QueueFlags kTransferOrComputeFamily =
        vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eCompute;

    auto queueFamilies = device.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if ((queue.queueFlags & kGraphicsFamily) && (queue.queueFlags & kTransferOrComputeFamily))
        {
            return {.graphics = {i, queue}, .transfer = {i, queue}};
        }
    }

    std::uint32_t graphicsIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t transferIndex = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if (graphicsIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & kGraphicsFamily))
        {
            graphicsIndex = i;
        }

        if (transferIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & kTransferOrComputeFamily))
        {
            transferIndex = i;
        }

        if (graphicsIndex != std::numeric_limits<std::uint32_t>::max() &&
            transferIndex != std::numeric_limits<std::uint32_t>::max())
        {
            break;
        }
    }

    if (graphicsIndex == std::numeric_limits<std::uint32_t>::max() ||
        transferIndex == std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error{"Could not find the Graphics and transfer queues"};
    }

    return {.graphics = {graphicsIndex, queueFamilies[graphicsIndex]},
            .transfer = {transferIndex, queueFamilies[transferIndex]}};
}

/**
 * Descriptor heaps are only usable if the dispatcher resolved their entry points. A device that
 * lists VK_EXT_descriptor_heap can still miss them: the loader or a layer in the way may not know
 * the extension. Untested: a loader that drops the entry points cannot be faked.
 */
void
requireHeapEntryPoints(const vk::raii::Device& device)
{
    const auto* dispatcher = device.getDispatcher();

    const std::pair<const char*, bool> entryPoints[] = {
        {"vkWriteResourceDescriptorsEXT", dispatcher->vkWriteResourceDescriptorsEXT != nullptr},
        {"vkWriteSamplerDescriptorsEXT", dispatcher->vkWriteSamplerDescriptorsEXT != nullptr},
        {"vkCmdBindResourceHeapEXT", dispatcher->vkCmdBindResourceHeapEXT != nullptr},
        {"vkCmdBindSamplerHeapEXT", dispatcher->vkCmdBindSamplerHeapEXT != nullptr},
        {"vkCmdPushDataEXT", dispatcher->vkCmdPushDataEXT != nullptr},
    };
    for (const auto& [name, resolved] : entryPoints)
    {
        if (!resolved)
        {
            throw std::runtime_error{std::format(
                "The device has VK_EXT_descriptor_heap, but {} did not resolve. Check the Vulkan "
                "loader and the layers (VK_LOADER_LAYERS_ENABLE, VK_ADD_LAYER_PATH) for one that "
                "does not know the extension.",
                name)};
        }
    }
}

vk::raii::Device
makeDevice(const vk::raii::PhysicalDevice& physicalDevice, const PhysicalDevice::QueueInfos& infos)
{
    const auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

    float                                  priority = 0.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueInfos;
    queueInfos.push_back({
        .queueFamilyIndex = infos.graphics.familyIndex,
        .queueCount       = 1u,
        .pQueuePriorities = &priority,
    });
    if (infos.transfer.familyIndex != infos.graphics.familyIndex)
    {
        queueInfos.push_back({
            .queueFamilyIndex = infos.transfer.familyIndex,
            .queueCount       = 1u,
            .pQueuePriorities = &priority,
        });
    }

    // Selection guarantees every kExtensions entry and kFeatures bit; see DeviceRequirements.
    std::vector<const char*> extensions{DeviceRequirements::kExtensions.begin(),
                                        DeviceRequirements::kExtensions.end()};
    if (hasExtension(availableExtensions, gPortabilityExtensionName))
    {
        extensions.push_back(gPortabilityExtensionName);
    }

    DeviceRequirements::Features features = DeviceRequirements::enabled();

    const vk::DeviceCreateInfo createInfo{
        .pNext                   = &features.get<vk::PhysicalDeviceFeatures2>(),
        .queueCreateInfoCount    = static_cast<std::uint32_t>(queueInfos.size()),
        .pQueueCreateInfos       = queueInfos.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    vk::raii::Device device{physicalDevice, createInfo};
    requireHeapEntryPoints(device);

    return device;
}
} // namespace

PhysicalDevice::PhysicalDevice(InstancePtr _instance, vk::raii::PhysicalDevice physicalDevice)
    : vk::raii::PhysicalDevice{std::move(physicalDevice)}
    , instance{std::move(_instance)}
    , queueInfos{findGraphicsTransferQueues(*this)}
{
}

const vk::raii::PhysicalDevice&
PhysicalDevice::raiiPhysicalDevice() const noexcept
{
    return *this;
}

const vk::raii::Device&
Device::raii() const noexcept
{
    return *this;
}

Device::Device(InstancePtr _instance, vk::raii::PhysicalDevice physicalDevice)
    : PhysicalDevice{std::move(_instance), std::move(physicalDevice)}
    , vk::raii::Device{makeDevice(raiiPhysicalDevice(), queueInfos)}
    , mGraphicsQueue{*this, queueInfos.graphics.familyIndex, queueInfos.graphics.familyProperties}
    , mTransferQueue{*this, queueInfos.transfer.familyIndex, queueInfos.transfer.familyProperties}
    , pipelineCache{*this, {}}
    , mMemoryAllocator{new MemoryAllocator{*this}}
{
}

Device::~Device() = default;

std::unique_ptr<Shader>
Device::createShader(Shader::ShadingStage stage, std::span<const std::uint32_t> binary)
{
    return std::unique_ptr<Shader>(new Shader{shared_from_this(), stage, binary});
}

SemaphorePtr
Device::createSemaphore()
{
    return SemaphorePtr{new Semaphore{shared_from_this()}};
}

TimelineSemaphore
Device::createTimelineSemaphore()
{
    return TimelineSemaphore{shared_from_this()};
}

std::shared_ptr<const Queue>
Device::getGraphicsQueue()
{
    return {shared_from_this(), &mGraphicsQueue};
}

std::shared_ptr<const Queue>
Device::getTransferQueue()
{
    return {shared_from_this(), &mTransferQueue};
}

std::shared_ptr<CommandBufferPool>
Device::createCommandBufferPool()
{
    return std::shared_ptr<CommandBufferPool>(new CommandBufferPool{shared_from_this()});
}

std::unique_ptr<GraphicsPipeline>
Device::createGraphicsPipeline(GraphicsPipeline::Parameters createInfo)
{
    return std::unique_ptr<GraphicsPipeline>(
        new GraphicsPipeline{shared_from_this(), std::move(createInfo)});
}

std::unique_ptr<Swapchain>
Device::createSwapchain(const Swapchain::SwapchainParameters& parameters)
{
    return std::unique_ptr<Swapchain>(new Swapchain{shared_from_this(), parameters});
}

std::unique_ptr<RenderBuffer>
Device::createRenderBuffer(AttachmentUsage         usage,
                           vk::Format              desiredFormat,
                           vk::Extent2D            extent,
                           SampleCount             sampleCount,
                           std::uint32_t           mipLevels,
                           std::uint32_t           arrayLevels,
                           vk::ImageTiling         imageTiling,
                           vk::ImageLayout         initialLayout,
                           vk::MemoryPropertyFlags memoryProperties)
{
    return std::unique_ptr<RenderBuffer>(new RenderBuffer{shared_from_this(),
                                                          usage,
                                                          desiredFormat,
                                                          extent,
                                                          sampleCount,
                                                          mipLevels,
                                                          arrayLevels,
                                                          imageTiling,
                                                          initialLayout,
                                                          memoryProperties});
}

std::unique_ptr<Buffer>
Device::createBuffer(const vk::BufferCreateInfo&                  createInfo,
                     const MemoryAllocator::AllocationParameters& allocationInfo)
{
    return mMemoryAllocator->makeBuffer(createInfo, allocationInfo);
}

DescriptorHeapPtr
Device::createDescriptorHeap(std::uint32_t                          resourceSlots,
                             std::span<const vk::SamplerCreateInfo> samplers)
{
    return DescriptorHeapPtr{new DescriptorHeap{shared_from_this(), resourceSlots, samplers}};
}

std::unique_ptr<Texture>
Device::createTexture(const DescriptorHeapPtr& heap, vk::Extent2D extent, vk::Format format)
{
    // The slot first: a full heap fails before any image exists.
    DescriptorHeap::Slot slot = heap->allocate();

    auto texture = mMemoryAllocator->makeTexture(
        {
            .imageType   = vk::ImageType::e2D,
            .format      = format,
            .extent      = {.width = extent.width, .height = extent.height, .depth = 1u},
            .mipLevels   = 1u,
            .arrayLayers = 1u,
            .samples     = vk::SampleCountFlagBits::e1,
            .tiling      = vk::ImageTiling::eOptimal,
            .usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        },
        std::move(slot));

    // The view is only a description of the descriptor: no VkImageView object exists.
    heap->writeImage(texture->mSlot,
                     {.image            = **texture,
                      .viewType         = vk::ImageViewType::e2D,
                      .format           = format,
                      .subresourceRange = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                           .levelCount = 1u,
                                           .layerCount = 1u}},
                     vk::ImageLayout::eShaderReadOnlyOptimal);

    return texture;
}

std::unique_ptr<UniformBlock>
Device::createUniformBlock(const DescriptorHeapPtr& heap, std::span<const std::byte> bytes)
{
    // A zero-sized buffer is invalid; rounding up to 16 keeps a shader's vec4 reads inside it.
    constexpr std::size_t kBlockGranularity = 16u;
    const std::size_t     size =
        std::max(kBlockGranularity,
                 (bytes.size() + kBlockGranularity - 1u) / kBlockGranularity * kBlockGranularity);

    DescriptorHeap::Slot slot = heap->allocate();

    const vk::DeviceSize alignment =
        raiiPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment;
    BufferPtr buffer =
        createBuffer({.size  = size,
                      .usage = vk::BufferUsageFlagBits::eUniformBuffer |
                               vk::BufferUsageFlagBits::eShaderDeviceAddress},
                     {.flags = AllocationFlag::eHostSequentialWrite, .alignment = alignment});
    {
        auto mapping = buffer->mapForWrite();
        std::fill_n(mapping.data, size, std::byte{0});
        std::ranges::copy(bytes, mapping.data);
    }

    const vk::DeviceAddress address = buffer->deviceAddress();
    // Untested: a driver that places the buffer below its own offset alignment reaches it.
    if (address % alignment != 0u)
    {
        throw std::runtime_error{"The uniform block got an address that is not aligned."};
    }
    heap->writeUniformBuffer(slot, {.address = address, .size = size});

    return std::unique_ptr<UniformBlock>(new UniformBlock{std::move(slot), std::move(buffer)});
}
} // namespace VOG::Graphics::Vulkan
