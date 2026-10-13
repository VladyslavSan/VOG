#include "VOG/Graphics/Frame/FrameObjectManager.hpp"
#include "VOG/Graphics/Frame/FrameObjects.hpp"
#include "VOG/Graphics/ShaderProgramCache.hpp"
#include "VOG/Graphics/Vulkan/Attachment/RenderBuffer.hpp"
#include "VOG/Graphics/Vulkan/Buffer.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"
#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <memory>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

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
        {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
         .usage = VMA_MEMORY_USAGE_AUTO});
}
} // namespace

/** Requirement 8 of ResourceOwnership.md; see VulkanFixture::expectDependentHoldsDevice(). */
class DeviceLifetime : public VulkanValidationFixture
{
};

/**
 * Device must not own FencePool. After exercising an external pool and the private allocator,
 * dropping the last DevicePtr (and pool) must destroy the device and its instance.
 */
TEST_F(DeviceLifetime, releasingLastReferenceDestroysDevice)
{
    auto fencePool = Graphics::Vulkan::FencePool::create(VulkanDevice);
    // Drop the handle so the pool is idle; then drop the pool itself.
    fencePool->getShared();
    fencePool.reset();

    auto buffer = makeBuffer(*VulkanDevice);
    buffer.reset();

    expectDeviceReleased(releaseDevice());
}

/**
 * The Device keeps its Instance alive. Plain fixture: no validation messenger needs the Instance
 * to outlive the Device by a controlled margin.
 */
TEST_F(VulkanFixture, Device_keepsInstanceAlive)
{
    const Graphics::Vulkan::InstanceWeakPtr weakInstance = VulkanInstance;
    const Graphics::Vulkan::DeviceWeakPtr   weakDevice   = VulkanDevice;

    VulkanInstance.reset();
    EXPECT_FALSE(weakInstance.expired());
    VulkanDevice->waitIdle();

    VulkanDevice.reset();
    EXPECT_TRUE(weakDevice.expired());
    EXPECT_TRUE(weakInstance.expired());
}

/**
 * A buffer may outlive every DevicePtr the caller holds. It keeps the device (and private VMA)
 * alive until freed.
 */
TEST_F(DeviceLifetime, bufferKeepsDeviceAlive)
{
    auto       buffer   = makeBuffer(*VulkanDevice);
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               buffer,
                               [](Graphics::Vulkan::Buffer& buffer)
                               {
                                   auto mapping    = buffer.mapForWrite();
                                   mapping.data[0] = std::byte{1};
                               });
}

TEST_F(DeviceLifetime, fenceKeepsDeviceAlive)
{
    auto       fence    = std::make_unique<Graphics::Vulkan::Fence>(VulkanDevice->createFence());
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               fence,
                               [](Graphics::Vulkan::Fence& fence)
                               {
                                   fence.useFence();
                                   EXPECT_EQ(fence.wait(0u), vk::Result::eTimeout);
                               });
}

/**
 * A live FenceHandle keeps the pool and therefore the device alive via FencePoolPtr → DevicePtr.
 */
TEST_F(DeviceLifetime, fenceHandleKeepsDeviceAlive)
{
    auto       handle   = Graphics::Vulkan::FencePool::create(VulkanDevice)->getShared();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               handle,
                               [](Graphics::Vulkan::FencePool::FenceHandle& handle)
                               {
                                   handle.useFence();
                                   EXPECT_EQ(handle.wait(0u), vk::Result::eTimeout);
                               });
}

TEST_F(DeviceLifetime, fencePoolKeepsDeviceAlive)
{
    auto       fencePool = Graphics::Vulkan::FencePool::create(VulkanDevice);
    const auto released  = releaseDevice();

    expectDependentHoldsDevice(released,
                               fencePool,
                               [](Graphics::Vulkan::FencePool& fencePool)
                               {
                                   fencePool.getShared();
                               });
}

TEST_F(DeviceLifetime, commandBufferPoolKeepsDeviceAlive)
{
    auto       pool     = VulkanDevice->createCommandBufferPool();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               pool,
                               [](Graphics::Vulkan::CommandBufferPool& pool)
                               {
                                   pool.get(vk::CommandBufferLevel::ePrimary);
                                   pool.reset();
                               });
}

/**
 * A handle keeps its pool, and through it the device, alive: user code holds nothing else.
 */
TEST_F(DeviceLifetime, commandBufferHandleKeepsDeviceAlive)
{
    auto handle = std::make_unique<Graphics::Vulkan::CommandBufferHandle>(
        VulkanDevice->createCommandBufferPool()->get(vk::CommandBufferLevel::ePrimary));
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               handle,
                               [](Graphics::Vulkan::CommandBufferHandle& handle)
                               {
                                   Graphics::Vulkan::CommandBufferRecorder recorder{**handle};
                                   recorder.finish();
                               });
}

TEST_F(DeviceLifetime, timelineSemaphoreKeepsDeviceAlive)
{
    auto semaphore = std::make_unique<Graphics::Vulkan::TimelineSemaphore>(
        VulkanDevice->createTimelineSemaphore());
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               semaphore,
                               [](Graphics::Vulkan::TimelineSemaphore& semaphore)
                               {
                                   EXPECT_EQ(semaphore.waitOnCPU(0u, 0u), vk::Result::eSuccess);
                               });
}

/**
 * The queue is owned by the device; the handle it hands out shares ownership of the device.
 */
TEST_F(DeviceLifetime, queueKeepsDeviceAlive)
{
    auto       queue    = VulkanDevice->getGraphicsQueue();
    auto       transfer = VulkanDevice->getTransferQueue();
    const auto released = releaseDevice();

    // The transfer handle alone must keep the device alive too; drop the graphics one first.
    queue.reset();
    expectDependentHoldsDevice(released,
                               transfer,
                               [](const Graphics::Vulkan::Queue& queue)
                               {
                                   queue.waitIdle();
                               });
}

/**
 * No Vulkan entry point is reachable through a bare render buffer, so the call under test is its
 * destruction (vkDestroyImageView / vkDestroyImage / vkFreeMemory on a live device).
 */
TEST_F(DeviceLifetime, renderBufferKeepsDeviceAlive)
{
    auto renderBuffer = VulkanDevice->createRenderBuffer(Graphics::Vulkan::AttachmentUsage::eColor,
                                                         vk::Format::eR8G8B8A8Unorm,
                                                         {.width = 16u, .height = 16u},
                                                         Graphics::Vulkan::SampleCount::e1,
                                                         1u,
                                                         1u,
                                                         vk::ImageTiling::eOptimal,
                                                         vk::ImageLayout::eUndefined,
                                                         vk::MemoryPropertyFlagBits::eDeviceLocal);
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, renderBuffer);
}

TEST_F(DeviceLifetime, frameObjectsKeepDeviceAlive)
{
    auto frame = std::make_unique<Graphics::Frame::FrameObjects>(VulkanDevice, std::size_t{1});
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               frame,
                               [](Graphics::Frame::FrameObjects& frame)
                               {
                                   frame.getCommandBufferPoolForThread(0u)->get(
                                       vk::CommandBufferLevel::ePrimary);
                               });
}

TEST_F(DeviceLifetime, frameObjectManagerKeepsDeviceAlive)
{
    auto manager = std::make_unique<Graphics::Frame::FrameObjectManager>(
        VulkanDevice, std::size_t{2}, std::size_t{1});
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               manager,
                               [](Graphics::Frame::FrameObjectManager& manager)
                               {
                                   manager.acquireNextFrame();
                               });
}

/**
 * The call under test is the lookup: the shader modules were created before the device was
 * released, and the cache exposes no other Vulkan entry point.
 */
TEST_F(DeviceLifetime, shaderProgramCacheKeepsDeviceAlive)
{
    auto cache = std::make_unique<Graphics::ShaderProgramCache>(VulkanDevice, VOG_TEST_SHADER_PATH);
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               cache,
                               [](Graphics::ShaderProgramCache& cache)
                               {
                                   EXPECT_TRUE(*cache.get("WorldSpace")->pipelineLayout);
                               });
}

/**
 * Repeated create/destroy cycles must not accumulate live devices.
 */
TEST_F(DeviceLifetime, createDestroyRepeatedly)
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
