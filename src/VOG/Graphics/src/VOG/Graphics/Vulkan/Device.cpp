#include "VOG/Graphics/Vulkan/Device.hpp"

#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/MemoryAllocator.hpp>

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

    float priority = 0.0f;
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
    : instance{std::move(_instance)}
    , PhysicalDevice{std::move(physicalDevice)}
    , vk::raii::Device{makeDevice(*this, queueInfos)}
    , graphicsQueue{*this, queueInfos.graphics.familyIndex, queueInfos.graphics.familyProperties}
    , transferQueue{*this, queueInfos.transfer.familyIndex, queueInfos.transfer.familyProperties}
    , pipelineCache{*this, {}}
{
}

void
Device::init()
{
    const_cast<MemoryAllocatorPtr&>(memoryAllocator) =
        std::shared_ptr<MemoryAllocator>(new MemoryAllocator{shared_from_this()});
    const_cast<FencePoolPtr&>(fencePool) =
        std::shared_ptr<FencePool>{new FencePool{shared_from_this()}};
}

const PhysicalDevice&
Device::getPhysicalDevice() const
{
    return *this;
}
} // namespace VOG::Graphics::Vulkan
