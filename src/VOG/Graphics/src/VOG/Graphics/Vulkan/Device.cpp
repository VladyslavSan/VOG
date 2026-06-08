#include "VOG/Graphics/Vulkan/Device.hpp"

#include <VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/DescriptorAllocator.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/FrameBuffer.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>
#include <VOG/Graphics/Vulkan/RenderPass.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <spdlog/spdlog.h>

namespace VOG::Graphics::Vulkan
{
namespace
{
const char* gPortabilityExtensionName = "VK_KHR_portability_subset";

constexpr std::vector<const char*>
getDeviceRequiredExtensions()
{
    std::vector<const char*> requiredExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};

    return requiredExtensions;
}

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

    std::vector<const char*> extensions = getDeviceRequiredExtensions();

    // Portability extension check
    {
        const auto found = std::find_if(availableExtensions.begin(),
                                        availableExtensions.end(),
                                        [](const auto& extension)
                                        {
                                            return std::strncmp(extension.extensionName,
                                                                gPortabilityExtensionName,
                                                                VK_MAX_EXTENSION_NAME_SIZE) == 0;
                                        });

        const bool portabilityExtensionFound = found != availableExtensions.end();
        if (portabilityExtensionFound)
        {
            extensions.push_back(gPortabilityExtensionName);
        }
    }

    // Consistency check, ensure that all added extensions are supported by the
    // device
    for (const auto& requestedExtension : extensions)
    {
        const bool extensionPresent =
            std::find_if(availableExtensions.begin(),
                         availableExtensions.end(),
                         [&requestedExtension](const auto& extension)
                         {
                             return std::strncmp(extension.extensionName,
                                                 requestedExtension,
                                                 VK_MAX_EXTENSION_NAME_SIZE) == 0;
                         }) != availableExtensions.end();

        if (!extensionPresent)
        {
            spdlog::error("Extension {} is not supported by the device.", requestedExtension);
        }
    }

    vk::PhysicalDeviceFeatures deviceFeatues{
        .depthClamp = 1u,
    };

    vk::StructureChain chain{
        vk::DeviceCreateInfo{
            .queueCreateInfoCount    = static_cast<std::uint32_t>(queueInfos.size()),
            .pQueueCreateInfos       = queueInfos.data(),
            .enabledExtensionCount   = static_cast<std::uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures        = &deviceFeatues,
        },
        vk::PhysicalDeviceVulkan12Features{
            .timelineSemaphore = 1u,
        },
        // Need this to make it work on macOS.
        vk::PhysicalDeviceSynchronization2Features{
            .synchronization2 = 1u,
        },
    };

    return {physicalDevice, chain.get()};
}
} // namespace

PhysicalDevice::PhysicalDevice(vk::raii::PhysicalDevice physicalDevice)
    : vk::raii::PhysicalDevice{std::move(physicalDevice)}
    , queueInfos{findGraphicsTransferQueues(*this)}
{
}

Device::Device(InstancePtr _instance, vk::raii::PhysicalDevice physicalDevice)
    : PhysicalDevice{std::move(physicalDevice)}
    , vk::raii::Device{makeDevice(*this, queueInfos)}
    , instance{std::move(_instance)}
    , graphicsQueue{*this, queueInfos.graphics.familyIndex, queueInfos.graphics.familyProperties}
    , transferQueue{*this, queueInfos.transfer.familyIndex, queueInfos.transfer.familyProperties}
    , pipelineCache{*this, {}}
{
}

void
Device::init()
{
    mMemoryAllocator = std::shared_ptr<MemoryAllocator>(new MemoryAllocator{shared_from_this()});
    mFencePool       = std::shared_ptr<FencePool>{new FencePool{shared_from_this()}};
}

Device::~Device() = default;

const FencePoolPtr&
Device::getFencePool() const
{
    return mFencePool;
}

ShaderPtr
Device::createShader(Shader::ShadingStage stage, std::span<const std::uint32_t> binary)
{
    return std::shared_ptr<Shader>(new Shader{shared_from_this(), stage, binary});
}

Fence
Device::createFence()
{
    return Fence{shared_from_this()};
}

TimelineSemaphore
Device::createTimelineSemaphore()
{
    return TimelineSemaphore{shared_from_this()};
}

std::shared_ptr<RenderPass>
Device::createRenderPass(const StaticVector<vk::AttachmentDescription,
                                            Limits::gMaxNumAttachments - 1u>& colorAttachments,
                         const vk::AttachmentDescription&                     depthStencil)
{
    return std::shared_ptr<RenderPass>(
        new RenderPass{shared_from_this(), colorAttachments, depthStencil});
}

std::shared_ptr<CommandBufferPool>
Device::createCommandBufferPool()
{
    return std::shared_ptr<CommandBufferPool>(new CommandBufferPool{shared_from_this()});
}

std::shared_ptr<Framebuffer>
Device::createFramebuffer(
    std::shared_ptr<RenderPass>                                           renderPass,
    StaticVector<AttachmentInterfacePtr, Limits::gMaxNumAttachments - 1u> colorAttachments,
    AttachmentInterfacePtr                                                depthStencilAttachment)
{
    return std::shared_ptr<Framebuffer>(new Framebuffer{shared_from_this(),
                                                        std::move(renderPass),
                                                        std::move(colorAttachments),
                                                        std::move(depthStencilAttachment)});
}

std::shared_ptr<ShaderProgram>
Device::createShaderProgram(ShaderProgram::ShadingStages stages)
{
    return std::shared_ptr<ShaderProgram>(new ShaderProgram{
        shared_from_this(), ShaderProgram::ShadingStagesChecked{std::move(stages)}});
}

std::shared_ptr<GraphicsPipeline>
Device::createGraphicsPipeline(GraphicsPipeline::ParametersLegacy createInfo)
{
    return std::shared_ptr<GraphicsPipeline>(
        new GraphicsPipeline{shared_from_this(), std::move(createInfo)});
}

std::shared_ptr<GraphicsPipeline>
Device::createGraphicsPipeline(GraphicsPipeline::Parameters createInfo)
{
    return std::shared_ptr<GraphicsPipeline>(
        new GraphicsPipeline{shared_from_this(), std::move(createInfo)});
}

std::shared_ptr<Swapchain>
Device::createSwapchain(const Swapchain::SwapchainParameters& parameters)
{
    return std::shared_ptr<Swapchain>(new Swapchain{shared_from_this(), parameters});
}

std::shared_ptr<RenderBuffer>
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
    return std::shared_ptr<RenderBuffer>(new RenderBuffer{shared_from_this(),
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

std::unique_ptr<DescriptorAllocator>
Device::createDescriptorAllocator(const DescriptorAllocator::ConstructionParameters& params)
{
    return std::unique_ptr<DescriptorAllocator>(
        new DescriptorAllocator{shared_from_this(), params});
}

std::unique_ptr<Buffer>
Device::createBuffer(const vk::BufferCreateInfo&                  createInfo,
                     const MemoryAllocator::AllocationParameters& allocationInfo)
{
    return mMemoryAllocator->makeBuffer(createInfo, allocationInfo);
}

const PhysicalDevice&
Device::getPhysicalDevice() const
{
    return *this;
}
} // namespace VOG::Graphics::Vulkan
