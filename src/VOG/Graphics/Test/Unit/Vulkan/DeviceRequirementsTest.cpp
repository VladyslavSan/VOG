#include "VOG/Graphics/Vulkan/DeviceRequirements.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>

#include "VulkanFixture.hpp"

using namespace VOG::Graphics::Vulkan;
using namespace std::string_view_literals;

namespace VOG::Tests
{
namespace
{
constexpr bool
requiresExtension(std::string_view name)
{
    return std::ranges::any_of(DeviceRequirements::kExtensions,
                               [name](const char* extension)
                               {
                                   return std::string_view{extension} == name;
                               });
}
} // namespace

// Dropping either extension from the table would let selection accept a device that cannot present
// safely.
static_assert(requiresExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME));
static_assert(requiresExtension(VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME));

/** The device the fixture created was selected by missing(), so nothing can be missing from it. */
TEST_F(VulkanFixture, DeviceRequirements_selectedDeviceMissesNothing)
{
    const vk::raii::PhysicalDevice& physicalDevice = *mDevice;

    const auto missing = DeviceRequirements::missing(physicalDevice);

    std::string joined;
    for (const std::string& item : missing)
    {
        joined += item + "; ";
    }
    EXPECT_TRUE(missing.empty()) << "missing: " << joined;
}

/** enabled() turns on every bit in kFeatures, which is what the device is created with. */
TEST(DeviceRequirements, enabledSetsEveryFeature)
{
    auto features = DeviceRequirements::enabled();

    for (const DeviceRequirements::Feature& feature : DeviceRequirements::kFeatures)
    {
        EXPECT_NE(feature.bit(features), 0u) << feature.name;
    }
}

TEST(DeviceRequirements, hasExtensionAndHasLayerMatchWholeNames)
{
    vk::ExtensionProperties extension{};
    std::ranges::copy("VK_EXT_debug_utils"sv, extension.extensionName.begin());
    vk::LayerProperties layer{};
    std::ranges::copy("VK_LAYER_KHRONOS_validation"sv, layer.layerName.begin());

    EXPECT_TRUE(hasExtension(std::span{&extension, 1}, "VK_EXT_debug_utils"));
    EXPECT_FALSE(hasExtension(std::span{&extension, 1}, "VK_EXT_debug"));
    EXPECT_FALSE(hasExtension({}, "VK_EXT_debug_utils"));
    EXPECT_TRUE(hasLayer(std::span{&layer, 1}, "VK_LAYER_KHRONOS_validation"));
    EXPECT_FALSE(hasLayer(std::span{&layer, 1}, "VK_LAYER_KHRONOS"));
}
} // namespace VOG::Tests
