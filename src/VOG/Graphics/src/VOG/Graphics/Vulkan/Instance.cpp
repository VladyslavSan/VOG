#include "VOG/Graphics/Vulkan/Instance.hpp"

#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
namespace
{
const char* gPortabilityEnumerationExtension = "VK_KHR_portability_enumeration";

constexpr std::vector<const char*>
getInstanceRequiredExtensions()
{
    // clang-format off
    std::vector<const char*> requiredExtensions{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        // Instance half of VK_EXT_swapchain_maintenance1, which the device requires.
        VK_EXT_SURFACE_MAINTENANCE_1_EXTENSION_NAME,
        VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME
    };
    // clang-format on

#ifdef PLATFORM_VIDEO_WINDOWS
    requiredExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(PLATFORM_VIDEO_APPLE)
    requiredExtensions.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
#endif
    // Linux WSI extensions are added opportunistically in makeInstance when present,
    // so headless CI (lavapipe without WSI) can still create an instance.

    return requiredExtensions;
}

vk::raii::Instance
makeInstance(const vk::raii::Context& context, const Instance::InstanceParameters& parameters)
{
    spdlog::info("Vulkan instance layers:");
    for (const auto& layer : context.enumerateInstanceLayerProperties())
    {
        spdlog::info("{}: implementationVersion{} info:{}",
                     static_cast<std::string_view>(layer.layerName),
                     layer.implementationVersion,
                     static_cast<std::string_view>(layer.description));
    }

    spdlog::info("Vulkan instance extensions:");
    const auto availableInstanceExtensions = context.enumerateInstanceExtensionProperties(nullptr);
    for (const auto& extension : availableInstanceExtensions)
    {
        spdlog::info("{}: specVersion{}",
                     static_cast<std::string_view>(extension.extensionName),
                     extension.specVersion);
    }

    const vk::ApplicationInfo appInfo = {
        .pApplicationName   = parameters.appName.c_str(),
        .applicationVersion = 1u,
        .pEngineName        = parameters.engineName.c_str(),
        .engineVersion      = 1u,
        .apiVersion         = gRequiredApiVersion,
    };

    const auto&              layers = parameters.layers;
    std::vector<const char*> layersAll(layers.size());
    std::transform(layers.begin(),
                   layers.end(),
                   layersAll.begin(),
                   [](auto& extension)
                   {
                       return extension.data();
                   });

    auto requiredExtensions = getInstanceRequiredExtensions();
    if (hasExtension(availableInstanceExtensions, gPortabilityEnumerationExtension))
    {
        requiredExtensions.push_back(gPortabilityEnumerationExtension);
    }

#ifdef PLATFORM_LINUX
    {
        auto addIfPresent = [&](const char* name)
        {
            if (hasExtension(availableInstanceExtensions, name))
            {
                requiredExtensions.push_back(name);
            }
        };
    #ifdef PLATFORM_VIDEO_LINUX_WAYLAND
        addIfPresent(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    #endif
    #ifdef PLATFORM_VIDEO_LINUX_XLIB
        addIfPresent(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    #endif
    }
#endif

    const auto&              extensions = parameters.extensions;
    std::vector<const char*> extensionsAll(extensions.size());
    std::transform(extensions.begin(),
                   extensions.end(),
                   extensionsAll.begin(),
                   [](auto& extension)
                   {
                       return extension.data();
                   });
    std::transform(requiredExtensions.begin(),
                   requiredExtensions.end(),
                   std::back_inserter(extensionsAll),
                   [](auto& extension)
                   {
                       return extension;
                   });

    // The caller's list and the required list can name the same extension; Vulkan wants each once.
    const auto byName = [](const char* name)
    {
        return std::string_view{name};
    };
    std::ranges::sort(extensionsAll, {}, byName);
    extensionsAll.erase(std::ranges::unique(extensionsAll, {}, byName).begin(),
                        extensionsAll.end());

    vk::InstanceCreateInfo instanceCreateInfo{
        .flags                   = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
        .pApplicationInfo        = &appInfo,
        .enabledLayerCount       = static_cast<std::uint32_t>(layersAll.size()),
        .ppEnabledLayerNames     = layersAll.data(),
        .enabledExtensionCount   = static_cast<std::uint32_t>(extensionsAll.size()),
        .ppEnabledExtensionNames = extensionsAll.data()};

    return {context, instanceCreateInfo};
}

void
logPhysicalDeviceProperties(const vk::PhysicalDeviceProperties& properties)
{
    spdlog::info("Device properties:");
    spdlog::info("Api version: {}.{}.{}",
                 VK_API_VERSION_MAJOR(properties.apiVersion),
                 VK_API_VERSION_MINOR(properties.apiVersion),
                 VK_API_VERSION_PATCH(properties.apiVersion));
    spdlog::info("Driver version: {}", properties.driverVersion);
    spdlog::info("Vendor ID: {}", properties.vendorID);
    spdlog::info("Device ID: {}", properties.deviceID);
    spdlog::info("Device name: {}", static_cast<std::string_view>(properties.deviceName));

    const auto& limits = properties.limits;
    spdlog::info("Device limits:");
    spdlog::info("maxImageDimension1D: {}", limits.maxImageDimension1D);
    spdlog::info("maxImageDimension2D: {}", limits.maxImageDimension2D);
    spdlog::info("maxImageDimension3D: {}", limits.maxImageDimension3D);

    spdlog::info("maxPushConstantsSize: {}", limits.maxPushConstantsSize);
    spdlog::info("maxBoundDescriptorSets: {}", limits.maxBoundDescriptorSets);
    spdlog::info(
        "maxFramebufferSize: {}x{}", limits.maxFramebufferWidth, limits.maxFramebufferHeight);
    spdlog::info("maxFramebufferLayers: {}", limits.maxFramebufferLayers);
    spdlog::info("maxColorAttachments: {}", limits.maxColorAttachments);
}

void
logDescriptorIndexingProperties(const vk::PhysicalDeviceDescriptorIndexingProperties& properties)
{
    spdlog::info("Device descriptor indexing properties:");
    spdlog::info("maxUpdateAfterBindDescriptorsInAllPools: {}",
                 properties.maxUpdateAfterBindDescriptorsInAllPools);
    spdlog::info("shaderUniformBufferArrayNonUniformIndexingNative: {}",
                 properties.shaderUniformBufferArrayNonUniformIndexingNative);
    spdlog::info("shaderSampledImageArrayNonUniformIndexingNative: {}",
                 properties.shaderSampledImageArrayNonUniformIndexingNative);
    spdlog::info("shaderStorageBufferArrayNonUniformIndexingNative: {}",
                 properties.shaderStorageBufferArrayNonUniformIndexingNative);
    spdlog::info("shaderStorageImageArrayNonUniformIndexingNative: {}",
                 properties.shaderStorageImageArrayNonUniformIndexingNative);
    spdlog::info("shaderInputAttachmentArrayNonUniformIndexingNative: {}",
                 properties.shaderInputAttachmentArrayNonUniformIndexingNative);
    spdlog::info("robustBufferAccessUpdateAfterBind: {}",
                 properties.robustBufferAccessUpdateAfterBind);
    spdlog::info("maxPerStageUpdateAfterBindResources: {}",
                 properties.maxPerStageUpdateAfterBindResources);
    spdlog::info("maxDescriptorSetUpdateAfterBindSamplers: {}",
                 properties.maxDescriptorSetUpdateAfterBindSamplers);
    spdlog::info("maxDescriptorSetUpdateAfterBindUniformBuffers: {}",
                 properties.maxDescriptorSetUpdateAfterBindUniformBuffers);
    spdlog::info("maxDescriptorSetUpdateAfterBindUniformBuffersDynamic: {}",
                 properties.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic);
    spdlog::info("maxDescriptorSetUpdateAfterBindStorageBuffers: {}",
                 properties.maxDescriptorSetUpdateAfterBindStorageBuffers);
    spdlog::info("maxDescriptorSetUpdateAfterBindStorageBuffersDynamic: {}",
                 properties.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic);
    spdlog::info("maxDescriptorSetUpdateAfterBindSampledImages: {}",
                 properties.maxDescriptorSetUpdateAfterBindSampledImages);
    spdlog::info("maxDescriptorSetUpdateAfterBindStorageImages: {}",
                 properties.maxDescriptorSetUpdateAfterBindStorageImages);
    spdlog::info("maxDescriptorSetUpdateAfterBindInputAttachments: {}",
                 properties.maxDescriptorSetUpdateAfterBindInputAttachments);
}

void
logExtensions(const std::vector<vk::ExtensionProperties>& extensions)
{
    spdlog::info("Available device extensions:");
    for (const auto& extension : extensions)
    {
        spdlog::info("{}: specVersion={}",
                     static_cast<std::string_view>(extension.extensionName),
                     extension.specVersion);
    }
}

vk::raii::PhysicalDevice
makePhysicalDevice(const vk::raii::Instance& instance)
{
    static std::once_flag sLogAvailableDevicesFlag       = {};
    static std::once_flag sLogSelectedDeviceCapabilities = {};

    vk::raii::PhysicalDevices physicalDevices{instance};

    std::call_once(sLogAvailableDevicesFlag,
                   [&]
                   {
                       spdlog::info("Available devices:");
                       for (std::size_t i = 0; i < physicalDevices.size(); ++i)
                       {
                           const auto& device     = physicalDevices[i];
                           const auto  properties = device.getProperties();
                           const auto  apiVersion = properties.apiVersion;
                           spdlog::info("[{}] {} (Vulkan {}.{}.{})",
                                        i,
                                        static_cast<std::string_view>(properties.deviceName),
                                        VK_API_VERSION_MAJOR(apiVersion),
                                        VK_API_VERSION_MINOR(apiVersion),
                                        VK_API_VERSION_PATCH(apiVersion));
                       }
                   });

    // The first device that offers everything VOG needs; otherwise report every device's gaps.
    std::string gaps;
    auto        selected = physicalDevices.begin();
    for (; selected != physicalDevices.end(); ++selected)
    {
        const std::vector<std::string> missing = DeviceRequirements::missing(*selected);
        if (missing.empty())
        {
            break;
        }

        gaps += std::format("\n  {} lacks:", selected->getProperties().deviceName.data());
        for (const std::string& item : missing)
        {
            gaps += ' ' + item + ';';
        }
    }
    if (selected == physicalDevices.end())
    {
        throw std::runtime_error{
            std::format("No physical device meets VOG's requirements ({} device(s) found).{}",
                        physicalDevices.size(),
                        gaps)};
    }

    vk::raii::PhysicalDevice physicalDevice = std::move(*selected);

    // Log device info.
    {
        const auto deviceProperties =
            physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                          vk::PhysicalDeviceDescriptorIndexingProperties>();
        const auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

        std::call_once(
            sLogSelectedDeviceCapabilities,
            [&]
            {
                const vk::PhysicalDeviceProperties& properties =
                    deviceProperties.get<vk::PhysicalDeviceProperties2>().properties;
                spdlog::info("Device name: \"{}\"",
                             static_cast<std::string_view>(properties.deviceName));

                logPhysicalDeviceProperties(properties);
                logDescriptorIndexingProperties(
                    deviceProperties.get<vk::PhysicalDeviceDescriptorIndexingProperties>());
                logExtensions(availableExtensions);
            });
    }

    return physicalDevice;
}
} // namespace

Instance::Instance(const InstanceParameters& parameters)
    : vk::raii::Instance{makeInstance(*this, parameters)}
{
}

const vk::raii::Instance&
Instance::raii() const noexcept
{
    return *this;
}

DevicePtr
Instance::makeDevice()
{
    vk::raii::PhysicalDevice physicalDevice = makePhysicalDevice(*this);

    return std::shared_ptr<Device>{new Device{shared_from_this(), std::move(physicalDevice)}};
}
} // namespace VOG::Graphics::Vulkan