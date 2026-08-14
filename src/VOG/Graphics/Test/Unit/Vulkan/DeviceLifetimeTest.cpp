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
 * Device must not own FencePool. After exercising an external pool and the private allocator,
 * dropping the last DevicePtr (and pool) must destroy the device.
 */
TEST(DeviceLifetime, releasingLastReferenceDestroysDevice)
{
    std::weak_ptr<Graphics::Vulkan::Device> weakDevice;

    {
        auto instance = makeInstance();
        auto device   = instance->makeDevice();
        weakDevice    = device;

        auto fencePool = std::make_shared<Graphics::Vulkan::FencePool>(device);
        // Drop the handle so the pool is idle; then drop the pool itself.
        fencePool->getShared();
        fencePool.reset();

        makeBuffer(*device);
    }

    EXPECT_TRUE(weakDevice.expired());
}

/**
 * A buffer may outlive every DevicePtr the caller holds. It keeps the device (and private VMA)
 * alive until freed.
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
 * A live FenceHandle keeps the pool and therefore the device alive via FencePoolPtr → DevicePtr.
 */
TEST(DeviceLifetime, fenceHandleKeepsDeviceAlive)
{
    std::weak_ptr<Graphics::Vulkan::Device>                   weakDevice;
    std::shared_ptr<Graphics::Vulkan::FencePool::FenceHandle> handle;

    {
        auto instance  = makeInstance();
        auto device    = instance->makeDevice();
        weakDevice     = device;
        auto fencePool = std::make_shared<Graphics::Vulkan::FencePool>(device);
        handle         = fencePool->getShared();
    }

    EXPECT_FALSE(weakDevice.expired());

    handle.reset();

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
