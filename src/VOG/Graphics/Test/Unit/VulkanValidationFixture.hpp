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
} // namespace VOG::Tests
