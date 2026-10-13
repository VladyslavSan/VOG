#pragma once

#include "VulkanFixture.hpp"

namespace VOG::Tests
{
/**
 * VulkanFixture with VK_LAYER_KHRONOS_validation enabled: ERROR-severity messages land in
 * validationErrors. When the layer is not installed the fixture still gives a working device and
 * validationActive() is false; tests that need the layer skip themselves.
 */
class VulkanValidationFixture : public VulkanFixture
{
public:
    VulkanValidationFixture()
        : VulkanFixture{true}
    {
    }
};
} // namespace VOG::Tests
