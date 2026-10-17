#pragma once

#include "VulkanFixture.hpp"

namespace VOG::Tests
{
/**
 * VulkanFixture with VK_LAYER_KHRONOS_validation enabled: ERROR-severity messages land in
 * mValidationErrors. When the layer is not installed every test of the fixture is skipped, because
 * an empty error list would pass for the wrong reason.
 */
class VulkanValidationFixture : public VulkanFixture
{
public:
    VulkanValidationFixture()
        : VulkanFixture{Options{.validation = true}}
    {
    }

protected:
    void
    SetUp() override
    {
        if (!validationActive())
        {
            GTEST_SKIP() << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils not available";
        }
    }
};

/**
 * VulkanValidationFixture with the synchronization validation of the layer on: a missing
 * dependency between two accesses is an ERROR, "SYNC-HAZARD-...". Skipped under the same
 * condition. Whether the setting reached the layer is not checked here; a test needs a negative
 * control that provokes a hazard.
 */
class VulkanSyncValidationFixture : public VulkanFixture
{
public:
    VulkanSyncValidationFixture()
        : VulkanFixture{Options{.validation = true, .syncValidation = true}}
    {
    }

protected:
    void
    SetUp() override
    {
        if (!validationActive())
        {
            GTEST_SKIP() << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils not available";
        }
    }
};
} // namespace VOG::Tests
