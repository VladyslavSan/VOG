#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Graphics::Vulkan
{
/**
 * What a physical device must offer for VOG to use it. One table serves selection (missing())
 * and device creation (kExtensions, enabled()), so the two cannot drift apart.
 */
struct DeviceRequirements
{
    using Features = vk::StructureChain<vk::PhysicalDeviceFeatures2,
                                        vk::PhysicalDeviceVulkan12Features,
                                        vk::PhysicalDeviceVulkan13Features,
                                        vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>;

    struct Feature
    {
        const char* name;
        /** The feature's bit inside @p features. */
        vk::Bool32& (*bit)(Features& features);
    };

    /**
     * Device extensions VOG enables. The EXT name of swapchain maintenance 1, not the KHR one: the
     * types and sType values are the same, and drivers and validation layers that predate the KHR
     * promotion expose only the EXT name.
     */
    static constexpr std::array kExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME,
                                               VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME};

    static constexpr std::array kFeatures = {
        Feature{"depthClamp",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceFeatures2>().features.depthClamp;
                }},
        Feature{"timelineSemaphore",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore;
                }},
        Feature{"synchronization2",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2;
                }},
        Feature{"dynamicRendering",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering;
                }},
        Feature{"maintenance4",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceVulkan13Features>().maintenance4;
                }},
        Feature{"swapchainMaintenance1",
                [](Features& features) -> vk::Bool32&
                {
                    return features.get<vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>()
                        .swapchainMaintenance1;
                }},
    };

    /** Every kFeatures bit set; chained into vkCreateDevice. */
    static Features enabled();

    /**
     * Names what @p device lacks: the API version, extensions from kExtensions and features from
     * kFeatures. Empty means the device is usable. Features are only listed for a device that has
     * the API version and all extensions, because querying them is not valid otherwise.
     */
    static std::vector<std::string> missing(const vk::raii::PhysicalDevice& device);
};

bool hasExtension(std::span<const vk::ExtensionProperties> extensions, std::string_view name);

bool hasLayer(std::span<const vk::LayerProperties> layers, std::string_view name);
} // namespace VOG::Graphics::Vulkan
