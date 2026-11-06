#include "VOG/Graphics/Frame/FrameObjectManager.hpp"
#include "VOG/Graphics/Frame/FrameObjects.hpp"
#include "VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp"
#include "VOG/Graphics/Vulkan/Buffer.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferPool.hpp"
#include "VOG/Graphics/Vulkan/CommandBufferRecorder.hpp"
#include "VOG/Graphics/Vulkan/FencePool.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>

#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

namespace VOG::Tests
{
// A wrapper that retains its Vulkan object must not be usable as the bare raii object: that would
// let a caller destroy or move the handle out from under the wrapper's owners.
static_assert(!std::is_convertible_v<Graphics::Vulkan::Semaphore&, vk::raii::Semaphore&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::TimelineSemaphore&, vk::raii::Semaphore&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::GraphicsPipeline&, vk::raii::Pipeline&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Buffer&, vk::raii::Buffer&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Texture&, vk::raii::Image&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::PhysicalDevice&, vk::raii::PhysicalDevice&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Device&, vk::raii::Device&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Device&, vk::raii::PhysicalDevice&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Instance&, vk::raii::Instance&>);
static_assert(!std::is_convertible_v<Graphics::Vulkan::Instance&, vk::raii::Context&>);
// Every destructor of an owner is noexcept: a failing wait is logged by inDestructor, not thrown.
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::FencePool::FenceHandle>);
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::CommandBufferPool>);
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::CommandBufferRecorder>);
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::ExecutableCommandBuffer>);
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::AcquiredSwapchainImage>);
static_assert(std::is_nothrow_destructible_v<Graphics::Vulkan::Swapchain::Presentation>);
// Without the defaulted moves a Presentation would copy on every move, and each copy would wait
// the present fence when dropped.
static_assert(std::is_nothrow_move_constructible_v<Graphics::Vulkan::Swapchain::Presentation>);
static_assert(!std::is_copy_constructible_v<Graphics::Vulkan::Swapchain::Presentation>);
// Assigning over a live one would drop its generation before its fence is waited.
static_assert(!std::is_move_assignable_v<Graphics::Vulkan::Swapchain::Presentation>);
static_assert(std::is_nothrow_destructible_v<Graphics::Frame::FrameObjects>);
// The slot owns a presentation and command buffer pools; a moved-from one has nothing to wait on.
static_assert(!std::is_move_constructible_v<Graphics::Frame::FrameObjects>);

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
        {.flags = Graphics::Vulkan::AllocationFlag::eHostSequentialWrite});
}
} // namespace

/** Requirement 8 of ResourceOwnership.md; see VulkanFixture::expectDependentHoldsDevice(). */
class DeviceLifetime : public VulkanValidationFixture
{
};

/** The validation layer on a headless instance, for a test that leaks a surface. */
class InstanceDestruction : public VulkanFixture
{
public:
    InstanceDestruction()
        : VulkanFixture{Options{.validation = true, .headlessSurface = true}}
    {
    }

protected:
    void
    SetUp() override
    {
        if (!validationActive() || !headlessSurfaceActive())
        {
            GTEST_SKIP() << "The validation layer or VK_EXT_headless_surface is not available";
        }
    }
};

/**
 * The sink also hears vkDestroyInstance, after every member of the Instance is gone: the reason
 * why its holder is the first base. A surface that nobody destroyed makes the layer report
 * VUID-vkDestroyInstance-instance-00629 then. The leak is on purpose.
 */
TEST_F(InstanceDestruction, sinkHearsErrorOfVkDestroyInstance)
{
    std::ignore =
        vk::raii::SurfaceKHR{mInstance->raii(), vk::HeadlessSurfaceCreateInfoEXT{}}.release();
    ASSERT_TRUE(mValidationErrors.empty());

    const auto released = releaseDevice();

    EXPECT_TRUE(released.instance.expired());
    const bool heard =
        std::ranges::any_of(mValidationErrors,
                            [](const std::string& message)
                            {
                                return message.contains("VUID-vkDestroyInstance-instance-00629");
                            });
    EXPECT_TRUE(heard) << mValidationErrors.size() << " error(s) heard; the first: "
                       << (mValidationErrors.empty() ? "" : mValidationErrors.front());
}

/**
 * Device must not own FencePool. After exercising an external pool and the private allocator,
 * dropping the last DevicePtr (and pool) must destroy the device and its instance.
 */
TEST_F(DeviceLifetime, releasingLastReferenceDestroysDevice)
{
    auto fencePool = Graphics::Vulkan::FencePool::create(mDevice);
    // Drop the handle so the pool is idle; then drop the pool itself.
    std::ignore = fencePool->take();
    fencePool.reset();

    auto buffer = makeBuffer(*mDevice);
    buffer.reset();

    expectDeviceReleased(releaseDevice());
}

/**
 * The Device keeps its Instance alive. Plain fixture: it watches nothing, and the test drops the
 * Instance and the Device by hand.
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
                                   auto mapping       = buffer.mapForWrite();
                                   mapping.bytes()[0] = std::byte{1};
                               });
}

/**
 * The heap keeps the device alive. No Vulkan entry point is reachable through a bare heap, so the
 * call under test is its destruction (vkDestroyBuffer of both heap buffers).
 */
TEST_F(DeviceLifetime, descriptorHeapKeepsDeviceAlive)
{
    auto       heap     = createHeap();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, heap);
}

/**
 * The texture keeps its heap, and through it the device, alive: the call under test is the
 * destruction of the image, after the heap's last other holder is gone.
 */
TEST_F(DeviceLifetime, textureKeepsDeviceAndHeapAlive)
{
    auto heap = createHeap();
    auto texture =
        mDevice->createTexture(heap, {.width = 2u, .height = 2u}, vk::Format::eR8G8B8A8Srgb);
    const Graphics::Vulkan::DescriptorHeapWeakPtr weakHeap = heap;
    heap.reset();
    EXPECT_FALSE(weakHeap.expired());

    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, texture);
    EXPECT_TRUE(weakHeap.expired());
}

/** As for the texture, for the heap buffer: it also keeps its buffer alive. */
TEST_F(DeviceLifetime, heapBufferKeepsDeviceAndHeapAlive)
{
    auto                                          heap     = createHeap();
    auto                                          block    = mDevice->createHeapBuffer(heap, 0u);
    const Graphics::Vulkan::DescriptorHeapWeakPtr weakHeap = heap;
    heap.reset();
    EXPECT_FALSE(weakHeap.expired());

    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, block);
    EXPECT_TRUE(weakHeap.expired());
}

/**
 * A submitted command buffer parks in its pool with the FenceHandle of its submit. The handle
 * retains the FencePool and the pool retains the device; the handle is not reachable on its own,
 * so the pool is the dependent, and its reset() waits the fence after the device was released.
 * It cannot fail independently of commandBufferPoolKeepsDeviceAlive: the pool alone keeps the
 * device alive, so the handle's own retention of the FencePool is never the only thing that does.
 */
TEST_F(DeviceLifetime, submittedCommandBufferKeepsDeviceAlive)
{
    auto fencePool = Graphics::Vulkan::FencePool::create(mDevice);
    auto pool      = mDevice->createCommandBufferPool();
    {
        auto executable = recordEmpty(pool);
        mDevice->getGraphicsQueue()->submit(*fencePool,
                                            {.commandBuffers = std::span{&executable, 1u}});
    }
    fencePool.reset();
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               pool,
                               [](Graphics::Vulkan::CommandBufferPool& pool)
                               {
                                   pool.reset();
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
                                   std::ignore = fencePool.take();
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
                                   {
                                       auto executable = recordEmpty(pool.shared_from_this());
                                   }
                                   pool.reset();
                               });
}

/**
 * An executable command buffer keeps its pool, and through it the device, alive: user code holds
 * nothing else. Its destruction (a discard, then freeing the buffer and the pool) is the call
 * under test.
 */
TEST_F(DeviceLifetime, executableCommandBufferKeepsDeviceAlive)
{
    std::optional<Graphics::Vulkan::ExecutableCommandBuffer> executable;
    executable.emplace(recordEmpty(mDevice->createCommandBufferPool()));
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released, executable);
}

/**
 * A recorder keeps its pool alive and ends the command buffer in finish(), a real Vulkan call.
 */
TEST_F(DeviceLifetime, commandBufferRecorderKeepsDeviceAlive)
{
    std::unique_ptr<Graphics::Vulkan::CommandBufferRecorder> recorder{
        new Graphics::Vulkan::CommandBufferRecorder(mDevice->createCommandBufferPool()->record())};
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               recorder,
                               [](Graphics::Vulkan::CommandBufferRecorder& recorder)
                               {
                                   auto executable = recorder.finish();
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

/** Like the binary semaphore: the call under test is its destruction. */
TEST_F(DeviceLifetime, timelineSemaphoreKeepsDeviceAlive)
{
    auto       semaphore = mDevice->createTimelineSemaphore();
    const auto released  = releaseDevice();

    expectDependentHoldsDevice(released, semaphore);
}

/**
 * The queue is owned by the device; the handle it hands out shares ownership of the device. The
 * test checks ownership only; no Vulkan call is involved: the graphics handle goes first, so the
 * transfer handle alone must keep the device alive. A real submit through the graphics handle is
 * covered by submittedCommandBufferKeepsDeviceAlive.
 */
TEST_F(DeviceLifetime, queueKeepsDeviceAlive)
{
    auto       queue    = mDevice->getGraphicsQueue();
    auto       transfer = mDevice->getTransferQueue();
    const auto released = releaseDevice();

    queue.reset();
    expectDependentHoldsDevice(released, transfer);
}

/**
 * No Vulkan entry point is reachable through a bare render target, so the call under test is its
 * destruction (vkDestroyImageView / vkDestroyImage / vmaFreeMemory on a live device).
 */
TEST_F(DeviceLifetime, renderTargetKeepsDeviceAlive)
{
    auto       renderTarget = createColorTarget(*mDevice, 16u);
    const auto released     = releaseDevice();

    expectDependentHoldsDevice(released, renderTarget);
}

TEST_F(DeviceLifetime, frameObjectsKeepDeviceAlive)
{
    auto       frame    = std::make_unique<Graphics::Frame::FrameObjects>(mDevice, std::size_t{1});
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               frame,
                               [](Graphics::Frame::FrameObjects& frame)
                               {
                                   auto executable =
                                       recordEmpty(frame.getCommandBufferPoolForThread(0u));
                               });
}

TEST_F(DeviceLifetime, frameObjectManagerKeepsDeviceAlive)
{
    auto manager = std::make_unique<Graphics::Frame::FrameObjectManager>(
        mDevice, Graphics::Frame::FrameObjectManager::Counts{.frames = 2u, .threads = 1u});
    const auto released = releaseDevice();

    expectDependentHoldsDevice(released,
                               manager,
                               [](Graphics::Frame::FrameObjectManager& manager)
                               {
                                   manager.prepareNextFrame();
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
