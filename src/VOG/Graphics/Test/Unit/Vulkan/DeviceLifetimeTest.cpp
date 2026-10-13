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

constexpr std::uint64_t gWaitForever = std::numeric_limits<std::uint64_t>::max();

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

/** Has the GPU signal @p fence through an empty submit; the fence must be unsignaled. */
void
submitSignalling(Graphics::Vulkan::Device& device, vk::Fence fence)
{
    device.getGraphicsQueue()->vk::raii::Queue::submit({}, fence);
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
    auto fencePool = Graphics::Vulkan::FencePool::create(mDevice);
    // Drop the handle so the pool is idle; then drop the pool itself.
    fencePool->getShared();
    fencePool.reset();

    auto buffer = makeBuffer(*mDevice);
    buffer.reset();

    expectDeviceReleased(releaseDevice());
}

/**
 * The Device keeps its Instance alive. Plain fixture: no validation messenger needs the Instance
 * to outlive the Device by a controlled margin.
 */
TEST_F(VulkanFixture, Device_keepsInstanceAlive)
{
    const Graphics::Vulkan::InstanceWeakPtr weakInstance = mInstance;
    const Graphics::Vulkan::DeviceWeakPtr   weakDevice   = mDevice;

    mInstance.reset();
    EXPECT_FALSE(weakInstance.expired());

    mDevice.reset();
    EXPECT_TRUE(weakDevice.expired());
    EXPECT_TRUE(weakInstance.expired());
}

/**
 * A buffer may outlive every DevicePtr the caller holds. It keeps the device (and private VMA)
 * alive until freed.
 */
TEST_F(DeviceLifetime, bufferKeepsDeviceAlive)
{
    auto       buffer   = makeBuffer(*mDevice);
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
    auto fence = std::make_unique<Graphics::Vulkan::Fence>(mDevice->createFence());
    submitSignalling(*mDevice, fence->handle());
    fence->markUsed();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               fence,
                               [](Graphics::Vulkan::Fence& fence)
                               {
                                   EXPECT_EQ(fence.wait(gWaitForever), vk::Result::eSuccess);
                               });
}

/**
 * A live FenceHandle keeps the pool and therefore the device alive via FencePoolPtr → DevicePtr.
 */
TEST_F(DeviceLifetime, fenceHandleKeepsDeviceAlive)
{
    auto handle = Graphics::Vulkan::FencePool::create(mDevice)->getShared();
    submitSignalling(*mDevice, handle->handle());
    handle->markUsed();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               handle,
                               [](Graphics::Vulkan::FencePool::FenceHandle& handle)
                               {
                                   EXPECT_EQ(handle.wait(gWaitForever), vk::Result::eSuccess);
                               });
}

TEST_F(DeviceLifetime, fencePoolKeepsDeviceAlive)
{
    auto       fencePool = Graphics::Vulkan::FencePool::create(mDevice);
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
    auto       pool     = mDevice->createCommandBufferPool();
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
        mDevice->createCommandBufferPool()->get(vk::CommandBufferLevel::ePrimary));
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               handle,
                               [](Graphics::Vulkan::CommandBufferHandle& handle)
                               {
                                   Graphics::Vulkan::CommandBufferRecorder recorder{**handle};
                                   recorder.finish();
                               });
}

/**
 * No Vulkan entry point is reachable through a bare binary semaphore, so the call under test is
 * its destruction (vkDestroySemaphore on a live device).
 */
TEST_F(DeviceLifetime, semaphoreKeepsDeviceAlive)
{
    auto       semaphore = mDevice->createSemaphore();
    const auto released  = releaseDevice();

    expectDependentHoldsDevice(released, semaphore);
}

TEST_F(DeviceLifetime, timelineSemaphoreKeepsDeviceAlive)
{
    auto semaphore =
        std::make_unique<Graphics::Vulkan::TimelineSemaphore>(mDevice->createTimelineSemaphore());
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
    auto       queue    = mDevice->getGraphicsQueue();
    auto       transfer = mDevice->getTransferQueue();
    const auto released = releaseDevice();

    // The transfer handle alone must keep the device alive too; drop the graphics one first.
    queue.reset();
    expectDependentHoldsDevice(released,
                               transfer,
                               [](const Graphics::Vulkan::Queue& queue)
                               {
                                   queue.vk::raii::Queue::submit({});
                               });
}

/**
 * No Vulkan entry point is reachable through a bare render buffer, so the call under test is its
 * destruction (vkDestroyImageView / vkDestroyImage / vkFreeMemory on a live device).
 */
TEST_F(DeviceLifetime, renderBufferKeepsDeviceAlive)
{
    auto       renderBuffer = mDevice->createRenderBuffer(Graphics::Vulkan::AttachmentUsage::eColor,
                                                          vk::Format::eR8G8B8A8Unorm,
                                                          {.width = 16u, .height = 16u},
                                                          Graphics::Vulkan::SampleCount::e1,
                                                          1u,
                                                          1u,
                                                          vk::ImageTiling::eOptimal,
                                                          vk::ImageLayout::eUndefined,
                                                          vk::MemoryPropertyFlagBits::eDeviceLocal);
    const auto released     = releaseDevice();

    expectDependentHoldsDevice(released, renderBuffer);
}

TEST_F(DeviceLifetime, frameObjectsKeepDeviceAlive)
{
    auto       frame    = std::make_unique<Graphics::Frame::FrameObjects>(mDevice, std::size_t{1});
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
        mDevice, std::size_t{2}, std::size_t{1});
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
    auto cache = std::make_unique<Graphics::ShaderProgramCache>(mDevice, VOG_TEST_SHADER_PATH);
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
