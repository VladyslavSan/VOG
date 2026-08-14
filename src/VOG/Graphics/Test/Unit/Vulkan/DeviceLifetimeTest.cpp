#include "VulkanFixture.hpp"

#include <gtest/gtest.h>

namespace VOG::Tests
{
/**
 * Creating and destroying devices repeatedly must not leak the Vulkan device via
 * Device↔FencePool/MemoryAllocator shared_ptr cycles.
 */
TEST(DeviceLifetime, createDestroyRepeatedly)
{
    for (int i = 0; i < 3; ++i)
    {
        auto instance = Graphics::Vulkan::Instance::create({
            .appName    = "VOG DeviceLifetime Test",
            .engineName = "Test",
            .layers     = {},
            .extensions = {},
        });
        auto device = instance->makeDevice();
        ASSERT_NE(device, nullptr);
        ASSERT_NE(device->getFencePool().getShared(), nullptr);

        device.reset();
        instance.reset();
    }
}
} // namespace VOG::Tests
