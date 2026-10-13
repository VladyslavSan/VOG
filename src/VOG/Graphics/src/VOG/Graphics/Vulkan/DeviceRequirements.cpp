#include "VOG/Graphics/Vulkan/DeviceRequirements.hpp"

#include <algorithm>
#include <format>
#include <type_traits>

namespace VOG::Graphics::Vulkan
{
DeviceRequirements::Features
DeviceRequirements::enabled()
{
    Features features;
    for (const Feature& feature : kFeatures)
    {
        feature.bit(features) = 1u;
    }

    return features;
}

std::vector<std::string>
DeviceRequirements::missing(const vk::raii::PhysicalDevice& device)
{
    std::vector<std::string> missing;

    if (device.getProperties().apiVersion < gRequiredApiVersion)
    {
        missing.push_back(std::format("Vulkan {}.{}",
                                      VK_API_VERSION_MAJOR(gRequiredApiVersion),
                                      VK_API_VERSION_MINOR(gRequiredApiVersion)));
    }

    const auto available = device.enumerateDeviceExtensionProperties();
    for (const char* extension : kExtensions)
    {
        if (!hasExtension(available, extension))
        {
            missing.emplace_back(extension);
        }
    }

    if (!missing.empty())
    {
        return missing;
    }

    auto supported = device.getFeatures2<vk::PhysicalDeviceFeatures2,
                                         vk::PhysicalDeviceVulkan12Features,
                                         vk::PhysicalDeviceVulkan13Features,
                                         vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>();
    static_assert(std::is_same_v<decltype(supported), Features>);

    for (const Feature& feature : kFeatures)
    {
        if (feature.bit(supported) == 0u)
        {
            missing.emplace_back(feature.name);
        }
    }

    return missing;
}

bool
hasExtension(std::span<const vk::ExtensionProperties> extensions, std::string_view name)
{
    return std::ranges::any_of(extensions,
                               [name](const vk::ExtensionProperties& extension)
                               {
                                   return std::string_view{extension.extensionName} == name;
                               });
}

bool
hasLayer(std::span<const vk::LayerProperties> layers, std::string_view name)
{
    return std::ranges::any_of(layers,
                               [name](const vk::LayerProperties& layer)
                               {
                                   return std::string_view{layer.layerName} == name;
                               });
}
} // namespace VOG::Graphics::Vulkan
