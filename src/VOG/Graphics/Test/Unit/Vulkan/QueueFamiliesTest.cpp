#include "VOG/Graphics/Vulkan/Device.hpp"

#include <gtest/gtest.h>

#include <array>
#include <stdexcept>

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
using enum vk::QueueFlagBits;

vk::QueueFamilyProperties
family(vk::QueueFlags flags)
{
    return {.queueFlags = flags, .queueCount = 1u};
}

// The family tables of real devices, in the order the driver lists them.
const std::array gNvidiaLike{
    family(eGraphics | eCompute | eTransfer), family(eTransfer), family(eCompute | eTransfer)};
const std::array gAmdLike{
    family(eGraphics | eCompute | eTransfer), family(eCompute | eTransfer), family(eTransfer)};

// lavapipe and an Intel GPU list one family that does everything.
const std::array gSingleFamily{family(eGraphics | eCompute | eTransfer)};
} // namespace

/**
 * With a dedicated transfer family wanted, a family with transfer and neither graphics nor compute
 * is the transfer family. The test runs the branch on tables only: no driver the tests run on has
 * two families, so this is the only check of it.
 */
TEST(QueueFamilies, selectsDedicatedTransferFamily)
{
    const PhysicalDevice::QueueInfos nvidia =
        PhysicalDevice::selectQueueFamilies(gNvidiaLike, true);
    EXPECT_EQ(nvidia.graphics.familyIndex, 0u);
    EXPECT_EQ(nvidia.transfer.familyIndex, 1u);
    EXPECT_EQ(nvidia.transfer.familyProperties.queueFlags, family(eTransfer).queueFlags);

    // The compute family before the transfer one is skipped.
    const PhysicalDevice::QueueInfos amd = PhysicalDevice::selectQueueFamilies(gAmdLike, true);
    EXPECT_EQ(amd.graphics.familyIndex, 0u);
    EXPECT_EQ(amd.transfer.familyIndex, 2u);
}

/**
 * Without a dedicated transfer family to take, the transfer queue is the graphics family's: a
 * device with one family, a device whose other family computes, a table in which the production
 * setting leaves the dedicated family unused. A device without a graphics family is rejected.
 */
TEST(QueueFamilies, fallsBackToGraphicsFamily)
{
    for (const bool dedicated : {false, true})
    {
        const auto single = PhysicalDevice::selectQueueFamilies(gSingleFamily, dedicated);
        EXPECT_EQ(single.graphics.familyIndex, 0u);
        EXPECT_EQ(single.transfer.familyIndex, 0u);

        const std::array computeOnly{family(eGraphics | eCompute | eTransfer),
                                     family(eCompute | eTransfer)};
        const auto       withCompute = PhysicalDevice::selectQueueFamilies(computeOnly, dedicated);
        EXPECT_EQ(withCompute.transfer.familyIndex, 0u);
    }

    // The graphics family is not always the first one.
    const std::array computeFirst{family(eCompute | eTransfer), family(eGraphics | eTransfer)};
    const auto       second = PhysicalDevice::selectQueueFamilies(computeFirst, false);
    EXPECT_EQ(second.graphics.familyIndex, 1u);
    EXPECT_EQ(second.transfer.familyIndex, 1u);

    // Production setting: the dedicated family exists but stays unused until an upload ran on one.
    // A tripwire: when kDedicatedTransferFamily is flipped to true, change this expectation to the
    // dedicated family (index 1 of gNvidiaLike) in the same commit.
    const auto production =
        PhysicalDevice::selectQueueFamilies(gNvidiaLike, PhysicalDevice::kDedicatedTransferFamily);
    EXPECT_EQ(production.transfer.familyIndex, production.graphics.familyIndex);

    EXPECT_THROW(PhysicalDevice::selectQueueFamilies(std::array{family(eTransfer)}, true),
                 std::runtime_error);
}
} // namespace VOG::Tests
