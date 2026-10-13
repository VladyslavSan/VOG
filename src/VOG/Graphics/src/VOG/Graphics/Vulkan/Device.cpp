#include "VOG/Graphics/Vulkan/Device.hpp"

#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

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

    return {physicalDevice, createInfo};
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

Fence
Device::createFence()
{
    return Fence{shared_from_this()};
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

std::unique_ptr<ShaderProgram>
Device::createShaderProgram(ShaderProgram::ShadingStages stages)
{
    return std::unique_ptr<ShaderProgram>(new ShaderProgram{
        shared_from_this(), ShaderProgram::ShadingStagesChecked{std::move(stages)}});
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
} // namespace VOG::Graphics::Vulkan
