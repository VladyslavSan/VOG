#include "VOG/Graphics/GraphicsProvider.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace VOG::Graphics
{
namespace
{
const char* gPortabilityExtensionName  = "VK_KHR_portability_subset";
const char* gSynchronizationLayer2Name = "VK_LAYER_KHRONOS_synchronization2";

std::vector<const char*>
getInstanceRequiredExtensions()
{
    std::vector<const char*> requiredExtensions{VK_KHR_SURFACE_EXTENSION_NAME};

#if defined(PLATFORM_VIDEO_WINDOWS)
    requiredExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(PLATFORM_VIDEO_APPLE)
    requiredExtensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif

    return requiredExtensions;
}

std::vector<const char*>
getDeviceRequiredExtensions()
{
    std::vector<const char*> requiredExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                                VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                                                VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME};

    return requiredExtensions;
}

vk::raii::Instance
MakeInstance(vk::raii::Context& context, const Common::JSONContainer& options)
{
    const auto          appName    = options["app_name"].getOr<std::string>("");
    const auto          engineName = options["engine_name"].getOr<std::string>("");
    vk::ApplicationInfo appInfo    = vk::ApplicationInfo()
                                      .setPApplicationName(appName.c_str())
                                      .setApplicationVersion(1u)
                                      .setPEngineName(engineName.c_str())
                                      .setEngineVersion(1u)
                                      .setApiVersion(VK_API_VERSION_1_3);

    const auto layers = options["layers"].getArrayOfType<std::string>();

    std::vector<const char*> layersAll(layers.size());
    std::transform(
        layers.begin(), layers.end(), layersAll.begin(), [](auto& e) { return e.c_str(); });
#ifdef PLATFORM_VIDEO_APPLE
    layersAll.push_back(gSynchronizationLayer2Name);
#endif

    const auto extensions         = options["extensions"].getArrayOfType<std::string>();
    const auto requiredExtensions = getInstanceRequiredExtensions();

    std::vector<const char*> extensionsAll(extensions.size());
    std::transform(extensions.begin(),
                   extensions.end(),
                   extensionsAll.begin(),
                   [](auto& e) { return e.c_str(); });
    std::transform(requiredExtensions.begin(),
                   requiredExtensions.end(),
                   std::back_inserter(extensionsAll),
                   [](auto& e) { return e; });

    [[maybe_unused]] const auto unique = std::unique(extensionsAll.begin(),
                                                     extensionsAll.end(),
                                                     [](const auto& first, const auto& second)
                                                     { return std::strcmp(first, second) == 0; });

    vk::InstanceCreateInfo instanceCreateInfo{
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<std::uint32_t>(layersAll.size()),
        .ppEnabledLayerNames     = layersAll.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(extensionsAll.size()),
        .ppEnabledExtensionNames = extensionsAll.data()};

    return {context, instanceCreateInfo};
}

vk::raii::PhysicalDevice
makePhysicalDevice(const vk::raii::Instance& instance)
{
    std::stringstream ss{};
    ss << "Available devices:" << std::endl;

    vk::raii::PhysicalDevices physicalDevices{instance};
    for (std::size_t i = 0; i < physicalDevices.size(); ++i)
    {
        const auto& device     = physicalDevices[i];
        const auto  properties = device.getProperties();

        ss << "[" << i << "] - " << properties.deviceName << std::endl;
    }

    std::cout << ss.str();

    return std::move(physicalDevices[0]);
}

GraphicsProvider::QueueInfos
FindGraphicsTransferQueues(vk::raii::PhysicalDevice& device)
{
    auto queueFamilies = device.getQueueFamilyProperties();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if ((queue.queueFlags & vk::QueueFlagBits::eGraphics) &&
            (queue.queueFlags & (vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eCompute)))
            return {.graphics = {i, queue}, .transfer = {i, queue}};
    }

    std::uint32_t graphicsIndex = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t transferIndex = std::numeric_limits<std::uint32_t>::max();
    for (std::uint32_t i = 0; i < queueFamilies.size(); ++i)
    {
        const auto& queue = queueFamilies[i];
        if (graphicsIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & vk::QueueFlagBits::eGraphics))
            graphicsIndex = i;

        if (transferIndex == std::numeric_limits<std::uint32_t>::max() &&
            (queue.queueFlags & vk::QueueFlagBits::eTransfer))
            transferIndex = i;

        if (graphicsIndex != std::numeric_limits<std::uint32_t>::max() &&
            transferIndex != std::numeric_limits<std::uint32_t>::max())
            break;
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
MakeDevice(vk::raii::PhysicalDevice& physicalDevice, const GraphicsProvider::QueueInfos& infos)
{
    const auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

    float                     priority = 0.0;
    vk::DeviceQueueCreateInfo queueInfo{};

    queueInfo.setQueueCount(1u)
        .setQueueFamilyIndex(infos.graphics.familyIndex)
        .setPQueuePriorities(&priority);

    std::vector<const char*> extensions = getDeviceRequiredExtensions();

    // Portability extension check
    {
        const auto found                     = std::find_if(availableExtensions.begin(),
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
    {
        for (const auto& requestedExtension : extensions)
        {
            const bool extensonPresent =
                std::find_if(availableExtensions.begin(),
                             availableExtensions.end(),
                             [&requestedExtension](const auto& extension)
                             {
                                 return std::strncmp(extension.extensionName,
                                                     requestedExtension,
                                                     VK_MAX_EXTENSION_NAME_SIZE) == 0;
                             }) != availableExtensions.end();

            if (!extensonPresent)
            {
                std::cout << "Extension \"" << requestedExtension
                          << "\" is not supported by the device." << std::endl;
            }
        }
    }

    vk::PhysicalDeviceVulkan12Features            vulkanDevice12Features{.timelineSemaphore = true};
    vk::PhysicalDeviceSynchronization2FeaturesKHR syncFeatures{.synchronization2 = true};
    vulkanDevice12Features.pNext = &syncFeatures;

    vk::DeviceCreateInfo deviceCreateInfo{.pNext                = &vulkanDevice12Features,
                                          .queueCreateInfoCount = 1u,
                                          .pQueueCreateInfos    = &queueInfo,
                                          .enabledExtensionCount =
                                              static_cast<std::uint32_t>(extensions.size()),
                                          .ppEnabledExtensionNames = extensions.data()};

    return {physicalDevice, deviceCreateInfo};
}
} // namespace

GraphicsProvider::~GraphicsProvider() {}

GraphicsProvider::GraphicsProvider(const Common::JSONContainer& options)
    : mInstance{MakeInstance(mContext, options)}
    , mPhysicalDevice{makePhysicalDevice(mInstance)}
    , mQueueInfos{FindGraphicsTransferQueues(mPhysicalDevice)}
    , mDevice{*this, MakeDevice(mPhysicalDevice, mQueueInfos)}
    , mGraphicsQueue{vk::raii::Queue{mDevice, mQueueInfos.graphics.familyIndex, 0u},
                     mQueueInfos.graphics.familyIndex,
                     mQueueInfos.graphics.familyProperties}
    , mTransferQueue{vk::raii::Queue{mDevice, mQueueInfos.transfer.familyIndex, 0u},
                     mQueueInfos.transfer.familyIndex,
                     mQueueInfos.transfer.familyProperties}
{
}

vk::raii::CommandPool
GraphicsProvider::makeCommandPool(CommandPoolType commandPoolType) const
{
    vk::CommandPoolCreateInfo ci{.queueFamilyIndex = mQueueInfos.graphics.familyIndex};
    switch (commandPoolType)
    {
    case CommandPoolType::eGraphics:
        ci.setQueueFamilyIndex(mQueueInfos.graphics.familyIndex);
        break;
    case CommandPoolType::eTransfer:
        ci.setQueueFamilyIndex(mQueueInfos.transfer.familyIndex);
        break;
    }
    return {mDevice, ci};
}
} // namespace VOG::Graphics
