#include "VOG/Graphics/Vulkan/Buffer.hpp"
#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <gtest/gtest.h>

#include <memory>

#include "VulkanFixture.hpp"

namespace VOG::Tests
{
namespace
{
constexpr vk::DeviceSize gBufferSize = 256u;

Graphics::Vulkan::InstancePtr
makeInstance()
{
    return Graphics::Vulkan::Instance::create({
        .appName    = "VOG DeviceLifetime Test",
        .engineName = "Test",
        .layers     = {},
        .extensions = {},
    });
}

std::unique_ptr<Graphics::Vulkan::Buffer>
makeBuffer(Graphics::Vulkan::Device& device)
{
    return device.createBuffer(
        {.size = gBufferSize, .usage = vk::BufferUsageFlagBits::eTransferSrc},
        {.usage = VMA_MEMORY_USAGE_AUTO});
}
} // namespace

/**
 * Nothing the device owns may retain the device. Once the fence pool and the memory allocator
 * have been exercised, dropping the last DevicePtr must still destroy the device.
 */
TEST(DeviceLifetime, releasingLastReferenceDestroysDevice)
{
    std::weak_ptr<Graphics::Vulkan::Device> weakDevice;

    {
        auto instance = makeInstance();
        auto device   = instance->makeDevice();
        weakDevice    = device;

        // Both fill a device owned cache: the fence goes back to the pool, the buffer's
        // allocation goes through the allocator.
        device->getFencePool().getShared();
        makeBuffer(*device);
    }

    EXPECT_TRUE(weakDevice.expired());
}

/**
 * A resource may outlive every DevicePtr the caller holds. It keeps the device alive until it is
 * itself destroyed, so freeing its allocation never runs against a destroyed VkDevice.
 */
TEST(DeviceLifetime, bufferKeepsDeviceAlive)
{
    std::weak_ptr<Graphics::Vulkan::Device>   weakDevice;
    std::unique_ptr<Graphics::Vulkan::Buffer> buffer;

    {
        auto instance = makeInstance();
        auto device   = instance->makeDevice();
        weakDevice    = device;
        buffer        = makeBuffer(*device);
    }

    EXPECT_FALSE(weakDevice.expired());

    buffer.reset();

    EXPECT_TRUE(weakDevice.expired());
}

/**
 * Repeated create/destroy cycles must not accumulate live devices.
 */
TEST(DeviceLifetime, createDestroyRepeatedly)
{
    for (int i = 0; i < 3; ++i)
    {
        std::weak_ptr<Graphics::Vulkan::Device> weakDevice;

        {
            auto instance = makeInstance();
            auto device   = instance->makeDevice();
            weakDevice    = device;
        }

        EXPECT_TRUE(weakDevice.expired());
    }
}
} // namespace VOG::Tests
