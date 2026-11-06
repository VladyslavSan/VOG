#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/DrawRecording.hpp"
#include "VOG/Engine/Renderer/FrameBlocks.hpp"
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineTable.hpp"
#include "VOG/Engine/Renderer/RendererImpl.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>
#include <VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/DescriptorHeap.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/FencePool.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include "HandleTestAccess.hpp"
#include "RenderQueueTestAccess.hpp"
#include "SyncValidationSetting.hpp"
#include "TestShaders.hpp"

#ifndef _WIN32
    #include <sys/mman.h>
#endif

using namespace VOG::Engine;

namespace VOG::Engine
{
/** Test-only window into the renderer, befriended by Renderer. */
struct RendererTestAccess
{
    /**
     * Joins the render thread, as ~Renderer does. The tests that read what the render thread
     * leaves behind call it first: the join orders everything the thread did before the reads, and
     * it ends the thread, so a test that joined submits no more frames. A packet that waits in the
     * queue is dropped by it.
     */
    static void
    join(Renderer& renderer)
    {
        renderer.mImpl->loop.stop();
    }

    /** After join(). */
    static Extent
    swapchainExtent(const Renderer& renderer)
    {
        const vk::Extent2D extent = renderer.mImpl->gpu.swapchain->getExtent();

        return {.width = extent.width, .height = extent.height};
    }

    /** After join(). */
    static std::size_t
    pipelineCount(const Renderer& renderer)
    {
        return renderer.mImpl->loop.mPipelines.mPipelines.size();
    }

    static FramePacketQueue&
    packets(Renderer& renderer)
    {
        return renderer.mImpl->packets;
    }

    /** The staging buffer of the job @p index that was pushed and that no iteration has taken yet;
     *  it must exist. Only while the render thread is held in a submit. */
    static std::weak_ptr<Graphics::Vulkan::Buffer>
    arrivedStaging(Renderer& renderer, std::size_t index)
    {
        const std::scoped_lock lock{renderer.mImpl->uploads.mMutex};

        return renderer.mImpl->uploads.mArrived.at(index).staging;
    }

    static Graphics::Vulkan::DevicePtr
    device(const Renderer& renderer)
    {
        return renderer.mImpl->gpu.device;
    }

    static std::shared_ptr<Graphics::Vulkan::DescriptorHeap>
    heap(const Renderer& renderer)
    {
        return renderer.mImpl->gpu.heap;
    }

    /** After join(): the size of the depth target, or 0 x 0 before the first frame. */
    static Extent
    depthExtent(const Renderer& renderer)
    {
        const auto& target = renderer.mImpl->loop.mDepthTarget;
        if (!target)
        {
            return {};
        }

        return {.width = target->getExtent2D().width, .height = target->getExtent2D().height};
    }

    static vk::Format
    depthFormat(const Renderer& renderer)
    {
        return renderer.mImpl->gpu.depthFormat;
    }

    static const vk::PhysicalDeviceLimits&
    limits(const Renderer& renderer)
    {
        return renderer.mImpl->gpu.device->limits;
    }

    /** After join(): the buffers of the Frame block, one per frame slot. */
    static std::vector<Graphics::Vulkan::HeapBufferPtr>
    frameBlocks(const Renderer& renderer)
    {
        return renderer.mImpl->gpu.frameBlocks.mBlocks;
    }

    /** Whether failure() returned the exception of the render thread. */
    static bool
    failureCollected(const Renderer& renderer)
    {
        return renderer.mImpl->loop.failureCollected();
    }

    /** The function table that every command buffer of the renderer's device records through. */
    static vk::raii::detail::DeviceDispatcher&
    dispatcher(Renderer& renderer)
    {
        // The device owns the table and keeps it mutable; getDispatcher() only hands it out const.
        return const_cast<vk::raii::detail::DeviceDispatcher&>(
            *renderer.mImpl->gpu.device->getDispatcher());
    }
};
} // namespace VOG::Engine

namespace VOG::Tests
{
namespace
{
constexpr Extent        gStartExtent{.width = 64u, .height = 64u};
constexpr std::uint32_t gResizedExtent      = 32u;
constexpr std::uint32_t gResizedAgainExtent = 48u;

/** How long a call that could block may take. A test that waits longer has found a hang. */
constexpr std::chrono::seconds gDeadline{5};

/**
 * Renders into a headless surface. When the validation layer is installed it runs with
 * synchronization validation on, and a validation error fails the test in TearDown(), also one
 * raised while the Renderer was constructed (Config::onValidationMessage).
 */
class RendererTest : public testing::Test
{
protected:
    void
    SetUp() override
    {
        const vk::raii::Context context;
        if (!Graphics::Vulkan::hasExtension(context.enumerateInstanceExtensionProperties(),
                                            "VK_EXT_headless_surface"))
        {
            GTEST_SKIP() << "VK_EXT_headless_surface not available";
        }

        mValidationInstalled =
            Graphics::Vulkan::hasLayer(context.enumerateInstanceLayerProperties(),
                                       "VK_LAYER_KHRONOS_validation") &&
            Graphics::Vulkan::hasExtension(context.enumerateInstanceExtensionProperties(),
                                           "VK_EXT_debug_utils");
        if (mValidationInstalled)
        {
            // The layer reads the setting in vkCreateInstance, and the environment is process-wide:
            // TearDown() restores it.
            mSyncValidationSetting.emplace();
        }
    }

    void
    TearDown() override
    {
        mSyncValidationSetting.reset();

        const std::scoped_lock lock{mErrors.mutex};
        EXPECT_TRUE(mErrors.messages.empty())
            << mErrors.messages.size() << " validation error(s); the first:\n"
            << (mErrors.messages.empty() ? "" : mErrors.messages.front());
    }

    Renderer::Config
    makeConfig(const std::filesystem::path& shaderPath = sharedTestShaderPath())
    {
        Renderer::Config config{
            .appName        = "VOG Renderer Test",
            .engineName     = "Test",
            .framesInFlight = 2u,
            .shaderPath     = shaderPath,
            .extent         = gStartExtent,
        };
        if (mValidationInstalled)
        {
            config.validation          = true;
            config.onValidationMessage = [&errors = mErrors](std::string_view message)
            {
                const std::scoped_lock lock{errors.mutex};
                errors.messages.emplace_back(message);
            };
        }

        return config;
    }

    static std::unique_ptr<Renderer>
    makeRenderer(const Renderer::Config& config)
    {
        return std::make_unique<Renderer>(
            Common::SurfaceHandle{.platform = Common::SurfaceHandle::Platform::eHeadless}, config);
    }

    std::unique_ptr<Renderer>
    makeRenderer(const std::filesystem::path& shaderPath = sharedTestShaderPath())
    {
        return makeRenderer(makeConfig(shaderPath));
    }

    bool mValidationInstalled = false;

    /** Removes the validation errors that name @p vuid; true if there was one. */
    bool
    takeError(std::string_view vuid)
    {
        const std::scoped_lock lock{mErrors.mutex};
        const auto             found = std::erase_if(mErrors.messages,
                                                     [vuid](const std::string& message)
                                                     {
                                             return message.contains(vuid);
                                                     });

        return found != 0u;
    }

private:
    /** The validation errors. Any thread that calls Vulkan reports them, the render thread too. */
    struct ErrorLog
    {
        std::mutex               mutex;
        std::vector<std::string> messages;
    };

    /** Declared first: the Renderers of a test write to it until they are gone. */
    ErrorLog                             mErrors;
    std::optional<SyncValidationSetting> mSyncValidationSetting;
};

/**
 * The positive control of the oracle: a known error on the renderer's device must reach the
 * sink. Without it a fixture that never saw an error would pass for the wrong reason. A
 * missing validation layer fails this test, because every other test here would then pass with
 * nothing watching.
 */
TEST_F(RendererTest, validationErrorOnTheRendererDeviceIsRecorded)
{
    ASSERT_TRUE(mValidationInstalled)
        << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils is missing, so no RendererTest can "
           "see a validation error.";
    auto renderer = makeRenderer();

    // A buffer of size 0 breaks VUID-VkBufferCreateInfo-size-00912, whether the driver takes it or
    // not.
    bool refused = false;
    try
    {
        std::ignore = RendererTestAccess::device(*renderer)->raii().createBuffer(
            {.size = 0u, .usage = vk::BufferUsageFlagBits::eTransferSrc});
    }
    catch (const vk::SystemError&)
    {
        refused = true;
    }

    EXPECT_TRUE(takeError("VUID-VkBufferCreateInfo-size-00912"))
        << (refused ? "The driver refused the buffer." : "The driver took the buffer.");
}

/** Runs a function for every frame; the tests' stand-in for a game object. */
class LambdaRenderable final : public Renderable
{
public:
    using Function = std::function<void(const FrameContext&, RenderQueue&)>;

    explicit LambdaRenderable(Function function)
        : mFunction{std::move(function)}
    {
    }

    void
    onDraw(const FrameContext& frameContext, RenderQueue& queue) override
    {
        mFunction(frameContext, queue);
    }

private:
    Function mFunction;
};

/** Notes the thread that destroys it. */
struct DestroyRecorder final : Renderable
{
    explicit DestroyRecorder(std::thread::id& destroyedOn)
        : destroyedOn{destroyedOn}
    {
    }

    ~DestroyRecorder() override { destroyedOn = std::this_thread::get_id(); }

    void
    onDraw(const FrameContext& /*frameContext*/, RenderQueue& /*queue*/) override
    {
    }

    std::thread::id& destroyedOn;
};

/**
 * Runs @p call on a helper thread and waits for it for gDeadline. If it has not returned by
 * then, the test fails and @p unblock runs, so that the helper can finish and be joined. An
 * exception of @p call reaches the test thread.
 */
template <class Call, class Unblock>
bool
returnsInTime(Call&& call, Unblock&& unblock)
{
    std::packaged_task<void()> task{std::forward<Call>(call)};
    std::future<void>          done = task.get_future();
    std::thread                helper{std::move(task)};

    const bool inTime = done.wait_for(gDeadline) == std::future_status::ready;
    if (!inTime)
    {
        ADD_FAILURE() << "A call did not return within " << gDeadline.count() << " s.";
        unblock();
    }
    helper.join();
    done.get();

    return inTime;
}

/** Polls @p condition until it holds or gDeadline passes. */
template <class Condition>
bool
becomesTrue(Condition&& condition)
{
    const auto deadline = std::chrono::steady_clock::now() + gDeadline;
    while (!condition())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    return true;
}

#ifndef _WIN32
/** Address space of @p size bytes that nothing may read or write: any access faults. */
class InaccessibleBytes
{
public:
    explicit InaccessibleBytes(std::size_t size)
        : mSize{size}
        , mData{mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0)}
    {
        if (mData == MAP_FAILED)
        {
            mData = nullptr;
        }
    }

    ~InaccessibleBytes()
    {
        if (mData != nullptr)
        {
            munmap(mData, mSize);
        }
    }

    InaccessibleBytes(const InaccessibleBytes&)            = delete;
    InaccessibleBytes(InaccessibleBytes&&)                 = delete;
    InaccessibleBytes& operator=(const InaccessibleBytes&) = delete;
    InaccessibleBytes& operator=(InaccessibleBytes&&)      = delete;

    /** Null if the mapping failed. */
    const void*
    data() const
    {
        return mData;
    }

    std::span<const std::byte>
    bytes() const
    {
        return {static_cast<const std::byte*>(mData), mSize};
    }

private:
    std::size_t mSize;
    void*       mData;
};
#endif

constexpr std::uint32_t gQuadStride = 24u;

/** As the WorldSpace shader reads them: a vec3 position (fed two floats) and a vec4 color. */
constexpr VertexLayout gCompatibleLayout = vertexLayout(
    gQuadStride,
    {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat4, sizeOf(VertexFormat::eFloat2)}});

/** WorldSpace also reads location 1, which this layout lacks. */
constexpr VertexLayout gIncompatibleLayout =
    vertexLayout(gQuadStride, {{VertexFormat::eFloat2, 0u}});

constexpr std::uint32_t gTexturedStride = 16u;
constexpr std::size_t   gPixelSize      = 4u;

/** As the Textured shader reads them: a position and a uv, two floats each. */
constexpr VertexLayout gTexturedLayout = vertexLayout(
    gTexturedStride,
    {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat2, sizeOf(VertexFormat::eFloat2)}});

Mesh
makeTexturedMesh(Renderer& renderer)
{
    return renderer.createMesh(
        {.name     = "textured",
         .vertices = std::vector<std::byte>(std::size_t{3u} * gTexturedStride),
         .layout   = gTexturedLayout});
}

/** A black 2x2 texture. */
Texture
makeTexture(Renderer& renderer)
{
    const std::vector<std::byte> pixels(std::size_t{2u} * 2u * gPixelSize);

    return renderer.createTexture({.pixels = pixels, .extent = {.width = 2u, .height = 2u}});
}

/** The Textured shader with a white tint and @p texture. */
Material
makeTexturedMaterial(Renderer& renderer, Texture texture)
{
    return renderer.createMaterial(
        {.shader     = renderer.getShader("Textured"),
         .parameters = {{"tint", Math::Vector4f{1.0f}}, {"albedo", std::move(texture)}}});
}

/** A material of the shader @p shader, which has no Material block. */
Material
makeMaterial(Renderer& renderer, std::string_view shader)
{
    return renderer.createMaterial({.shader = renderer.getShader(shader)});
}

/** A mesh of three vertices of @p layout, the size of which is gQuadStride of them, all zero. */
Mesh
makeMesh(Renderer& renderer, const VertexLayout& layout, std::string name = "mesh")
{
    return renderer.createMesh({.name     = std::move(name),
                                .vertices = std::vector<std::byte>(std::size_t{3u} * layout.stride),
                                .layout   = layout});
}

/** The bytes that the staging buffer of a mesh of gCompatibleLayout holds. */
constexpr std::uint64_t gMeshBytes = std::uint64_t{3u} * gQuadStride;

/**
 * Makes the ring of the upload queue let go of what its earlier copies hold: the pools of its
 * command buffers reset when they are used again, which is two copies later. Each of the two
 * small meshes is a copy of its own, waited for.
 */
void releaseTheUploadRing(Renderer& renderer);

/** A renderable that draws @p mesh with @p material in every frame. */
std::shared_ptr<Renderable>
drawing(Mesh mesh, Material material)
{
    return std::make_shared<LambdaRenderable>(
        [mesh = std::move(mesh), material = std::move(material)](const FrameContext& /*context*/,
                                                                 RenderQueue& queue)
        {
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
        });
}

std::atomic<int> gPipelineBinds;
std::atomic<int> gVertexBufferBinds;
std::atomic<int> gIndexBufferBinds;

PFN_vkCmdBindPipeline      gRealBindPipeline      = nullptr;
PFN_vkCmdBindVertexBuffers gRealBindVertexBuffers = nullptr;
PFN_vkCmdBindIndexBuffer   gRealBindIndexBuffer   = nullptr;

VKAPI_ATTR void VKAPI_CALL
countBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint, VkPipeline pipeline)
{
    ++gPipelineBinds;
    gRealBindPipeline(commandBuffer, bindPoint, pipeline);
}

VKAPI_ATTR void VKAPI_CALL
countBindVertexBuffers(VkCommandBuffer     commandBuffer,
                       uint32_t            firstBinding,
                       uint32_t            bindingCount,
                       const VkBuffer*     buffers,
                       const VkDeviceSize* offsets)
{
    ++gVertexBufferBinds;
    gRealBindVertexBuffers(commandBuffer, firstBinding, bindingCount, buffers, offsets);
}

VKAPI_ATTR void VKAPI_CALL
countBindIndexBuffer(VkCommandBuffer commandBuffer,
                     VkBuffer        buffer,
                     VkDeviceSize    offset,
                     VkIndexType     indexType)
{
    ++gIndexBufferBinds;
    gRealBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

/**
 * Counts the bind commands that reach the driver, by swapping them in the device's function table
 * for counting forwarders. Scope it around a stopped renderer: the render thread reads the table.
 */
class BindCounter
{
public:
    explicit BindCounter(vk::raii::detail::DeviceDispatcher& table)
        : mTable{table}
    {
        gPipelineBinds = gVertexBufferBinds = gIndexBufferBinds = 0;
        gRealBindPipeline                                       = table.vkCmdBindPipeline;
        gRealBindVertexBuffers                                  = table.vkCmdBindVertexBuffers;
        gRealBindIndexBuffer                                    = table.vkCmdBindIndexBuffer;
        table.vkCmdBindPipeline                                 = countBindPipeline;
        table.vkCmdBindVertexBuffers                            = countBindVertexBuffers;
        table.vkCmdBindIndexBuffer                              = countBindIndexBuffer;
    }

    ~BindCounter()
    {
        mTable.vkCmdBindPipeline      = gRealBindPipeline;
        mTable.vkCmdBindVertexBuffers = gRealBindVertexBuffers;
        mTable.vkCmdBindIndexBuffer   = gRealBindIndexBuffer;
    }

    BindCounter(const BindCounter&)            = delete;
    BindCounter& operator=(const BindCounter&) = delete;

private:
    vk::raii::detail::DeviceDispatcher& mTable;
};

/** One call of vkQueueSubmit2 that a QueueSpy saw. */
struct SpiedSubmit
{
    /** Copies (vkCmdCopyBuffer and vkCmdCopyBufferToImage) recorded since the call before. */
    std::size_t copies = 0u;
    /** The binary semaphore it signals: that of a frame slot for a frame's submit, null for a
     *  submit of copies, which signals a timeline semaphore only. */
    VkSemaphore signal = VK_NULL_HANDLE;
};

std::mutex               gSpyMutex;
std::condition_variable  gSpyChanged;
std::vector<SpiedSubmit> gSpiedSubmits;
std::size_t              gSpiedCopies = 0u;
bool                     gSpyHoldNext = false;
bool                     gSpyHeld     = false;
bool                     gSpyReleased = false;

PFN_vkQueueSubmit2         gRealQueueSubmit2      = nullptr;
PFN_vkCmdCopyBuffer        gRealCopyBuffer        = nullptr;
PFN_vkCmdCopyBufferToImage gRealCopyBufferToImage = nullptr;

VKAPI_ATTR void VKAPI_CALL
spyCopyBuffer(VkCommandBuffer     commandBuffer,
              VkBuffer            source,
              VkBuffer            destination,
              uint32_t            regionCount,
              const VkBufferCopy* regions)
{
    {
        const std::scoped_lock lock{gSpyMutex};
        ++gSpiedCopies;
    }
    gRealCopyBuffer(commandBuffer, source, destination, regionCount, regions);
}

VKAPI_ATTR void VKAPI_CALL
spyCopyBufferToImage(VkCommandBuffer          commandBuffer,
                     VkBuffer                 source,
                     VkImage                  destination,
                     VkImageLayout            layout,
                     uint32_t                 regionCount,
                     const VkBufferImageCopy* regions)
{
    {
        const std::scoped_lock lock{gSpyMutex};
        ++gSpiedCopies;
    }
    gRealCopyBufferToImage(commandBuffer, source, destination, layout, regionCount, regions);
}

VKAPI_ATTR VkResult VKAPI_CALL
spyQueueSubmit2(VkQueue queue, uint32_t submitCount, const VkSubmitInfo2* submits, VkFence fence)
{
    {
        std::unique_lock lock{gSpyMutex};
        SpiedSubmit      spied{.copies = std::exchange(gSpiedCopies, 0u)};
        for (const VkSubmitInfo2& submit : std::span{submits, submitCount})
        {
            for (const VkSemaphoreSubmitInfo& signal :
                 std::span{submit.pSignalSemaphoreInfos, submit.signalSemaphoreInfoCount})
            {
                // The value of a binary semaphore is ignored and 0; a timeline signal rises.
                if (signal.value == 0u)
                {
                    spied.signal = signal.semaphore;
                }
            }
        }
        gSpiedSubmits.push_back(spied);

        if (std::exchange(gSpyHoldNext, false))
        {
            gSpyHeld = true;
            gSpyChanged.notify_all();
            // A test that fails before it releases must not hang the render thread for ever.
            gSpyChanged.wait_for(lock,
                                 2 * gDeadline,
                                 []
                                 {
                                     return gSpyReleased;
                                 });
            gSpyReleased = false;
            gSpyHeld     = false;
        }
    }

    return gRealQueueSubmit2(queue, submitCount, submits, fence);
}

/**
 * Sees the submits and the copies of the render thread, by swapping them in the device's function
 * table for forwarders that note them, like BindCounter. It can also hold the render thread in its
 * next submit: what is created meanwhile waits in the upload queue, and no iteration takes it. The
 * thread that holds it is the test's; the call that waits is bounded by a deadline.
 *
 * Make it while the render thread is not recording (before anything is created, or after a
 * join()): the thread reads the table.
 */
// The state is in globals, because the forwarders it swaps in are plain functions; the members are
// the interface of the instance.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
class QueueSpy
{
public:
    explicit QueueSpy(vk::raii::detail::DeviceDispatcher& table)
        : mTable{table}
    {
        gSpiedSubmits.clear();
        gSpiedCopies = 0u;
        gSpyHoldNext = gSpyHeld = gSpyReleased = false;

        gRealQueueSubmit2            = table.vkQueueSubmit2;
        gRealCopyBuffer              = table.vkCmdCopyBuffer;
        gRealCopyBufferToImage       = table.vkCmdCopyBufferToImage;
        table.vkQueueSubmit2         = spyQueueSubmit2;
        table.vkCmdCopyBuffer        = spyCopyBuffer;
        table.vkCmdCopyBufferToImage = spyCopyBufferToImage;
    }

    ~QueueSpy()
    {
        release();
        mTable.vkQueueSubmit2         = gRealQueueSubmit2;
        mTable.vkCmdCopyBuffer        = gRealCopyBuffer;
        mTable.vkCmdCopyBufferToImage = gRealCopyBufferToImage;
    }

    QueueSpy(const QueueSpy&)            = delete;
    QueueSpy& operator=(const QueueSpy&) = delete;

    /** The next submit of the render thread, or of any thread, waits for release(). */
    void
    holdNextSubmit()
    {
        const std::scoped_lock lock{gSpyMutex};
        gSpyHoldNext = true;
    }

    /** Waits for gDeadline until a submit is held; false if none was. */
    [[nodiscard]] bool
    waitUntilHeld()
    {
        std::unique_lock lock{gSpyMutex};

        return gSpyChanged.wait_for(lock,
                                    gDeadline,
                                    []
                                    {
                                        return gSpyHeld;
                                    });
    }

    void
    release()
    {
        {
            const std::scoped_lock lock{gSpyMutex};
            gSpyReleased = true;
        }
        gSpyChanged.notify_all();
    }

    /** The submits so far, in order. */
    [[nodiscard]] std::vector<SpiedSubmit>
    submits() const
    {
        const std::scoped_lock lock{gSpyMutex};

        return gSpiedSubmits;
    }

    /** The copies of each submit of copies, in order: the submits that signal no binary
     *  semaphore. */
    [[nodiscard]] std::vector<std::size_t>
    copiesPerUpload() const
    {
        std::vector<std::size_t> copies;
        for (const SpiedSubmit& submit : submits())
        {
            if (submit.signal == VK_NULL_HANDLE)
            {
                copies.push_back(submit.copies);
            }
        }

        return copies;
    }

private:
    vk::raii::detail::DeviceDispatcher& mTable;
};

// NOLINTEND(readability-convert-member-functions-to-static)

PFN_vkAcquireNextImageKHR gRealAcquireNextImage = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL
failAcquireNextImage(VkDevice /*device*/,
                     VkSwapchainKHR /*swapchain*/,
                     uint64_t /*timeout*/,
                     VkSemaphore /*semaphore*/,
                     VkFence /*fence*/,
                     uint32_t* /*imageIndex*/)
{
    return VK_ERROR_SURFACE_LOST_KHR;
}

/**
 * Makes every acquire of the swapchain image fail as a lost surface does: the render thread gets a
 * vk::SurfaceLostKHRError, the one failure of the render thread that a driver, not the renderer,
 * causes. Scope it around a renderer that renders nothing yet.
 */
class SurfaceLoss
{
public:
    explicit SurfaceLoss(vk::raii::detail::DeviceDispatcher& table)
        : mTable{table}
    {
        gRealAcquireNextImage       = table.vkAcquireNextImageKHR;
        table.vkAcquireNextImageKHR = failAcquireNextImage;
    }

    ~SurfaceLoss() { mTable.vkAcquireNextImageKHR = gRealAcquireNextImage; }

    SurfaceLoss(const SurfaceLoss&)            = delete;
    SurfaceLoss& operator=(const SurfaceLoss&) = delete;

private:
    vk::raii::detail::DeviceDispatcher& mTable;
};

/** What one vkCmdBeginRendering of the render thread asked for. */
struct SpiedRendering
{
    std::uint32_t        colorCount = 0u;
    vk::AttachmentLoadOp colorLoadOp{};
    std::array<float, 4> colorClear{};
    bool                 hasDepth = false;
    vk::AttachmentLoadOp depthLoadOp{};
    float                depthClear = 0.0f;
};

std::mutex                  gRenderingMutex;
std::vector<SpiedRendering> gSpiedRenderings;
PFN_vkCmdBeginRendering     gRealBeginRendering = nullptr;

VKAPI_ATTR void VKAPI_CALL
spyBeginRendering(VkCommandBuffer commandBuffer, const VkRenderingInfo* info)
{
    SpiedRendering spied{.colorCount = info->colorAttachmentCount};
    if (info->colorAttachmentCount > 0u)
    {
        const VkRenderingAttachmentInfo& color = info->pColorAttachments[0];
        spied.colorLoadOp                      = static_cast<vk::AttachmentLoadOp>(color.loadOp);
        std::ranges::copy(std::span{color.clearValue.color.float32}, spied.colorClear.begin());
    }
    if (info->pDepthAttachment != nullptr)
    {
        spied.hasDepth    = true;
        spied.depthLoadOp = static_cast<vk::AttachmentLoadOp>(info->pDepthAttachment->loadOp);
        spied.depthClear  = info->pDepthAttachment->clearValue.depthStencil.depth;
    }
    {
        const std::scoped_lock lock{gRenderingMutex};
        gSpiedRenderings.push_back(spied);
    }

    gRealBeginRendering(commandBuffer, info);
}

/**
 * Notes what each vkCmdBeginRendering of the render thread asked for, by swapping the call in the
 * device's function table for a forwarder, like BindCounter: the clear color, the depth attachment
 * and its load op of a real frame, which no swapchain image lets a test read back. Make it while
 * the render thread is idle, and read after a join().
 */
// The state is in globals, because the forwarder it swaps in is a plain function; the members are
// the interface of the instance.
// NOLINTBEGIN(readability-convert-member-functions-to-static)
class RenderingSpy
{
public:
    explicit RenderingSpy(vk::raii::detail::DeviceDispatcher& table)
        : mTable{table}
    {
        gSpiedRenderings.clear();
        gRealBeginRendering       = table.vkCmdBeginRendering;
        table.vkCmdBeginRendering = spyBeginRendering;
    }

    ~RenderingSpy() { mTable.vkCmdBeginRendering = gRealBeginRendering; }

    RenderingSpy(const RenderingSpy&)            = delete;
    RenderingSpy& operator=(const RenderingSpy&) = delete;

    [[nodiscard]] std::vector<SpiedRendering>
    renderings() const
    {
        const std::scoped_lock lock{gRenderingMutex};

        return gSpiedRenderings;
    }

private:
    vk::raii::detail::DeviceDispatcher& mTable;
};

// NOLINTEND(readability-convert-member-functions-to-static)

void
releaseTheUploadRing(Renderer& renderer)
{
    for (int copy = 0; copy < 2; ++copy)
    {
        const Mesh mesh = makeMesh(renderer, gCompatibleLayout);
        EXPECT_TRUE(becomesTrue(
            [&]
            {
                return mesh.isResident();
            }));
    }
}
} // namespace

/**
 * Dropping the renderer from another thread tears it down on that thread: the render thread owns
 * nothing, so it cannot be the one that runs ~Renderer. The renderable dies there too.
 *
 * It proves liveness and thread affinity. The self-join it is named for can no longer be
 * written, so there is no broken version of the code for it to fail against.
 */
TEST_F(RendererTest, destroyWhileFrameInFlightJoins)
{
    std::thread::id destroyedOn;

    auto                            renderable = std::make_shared<DestroyRecorder>(destroyedOn);
    const std::weak_ptr<Renderable> weak       = renderable;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::move(renderable));
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    std::thread           dropper{[&renderer]
                                  {
                            renderer.reset();
                                  }};
    const std::thread::id dropperId = dropper.get_id();
    dropper.join();

    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(destroyedOn, dropperId);
}

/** ~Renderer wakes a render thread that waits for the next frame or for uploads. */
TEST_F(RendererTest, destroyingAnIdleRendererReturns)
{
    auto              renderer = makeRenderer();
    FramePacketQueue& packets  = RendererTestAccess::packets(*renderer);

    EXPECT_TRUE(returnsInTime(
        [&]
        {
            renderer.reset();
        },
        [&]
        {
            // The thread has not seen the stop: a packet wakes it, and it ends at the next check.
            packets.push({}, {});
        }));
}

/**
 * An onDraw() that throws reaches the caller of submitFrame(), the main thread: the renderer keeps
 * running, and the next frame is submitted as usual.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, throwingOnDrawPropagatesFromSubmitFrame)
{
    bool thrown = false;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&thrown](const FrameContext& /*context*/, RenderQueue& /*queue*/)
        {
            if (!thrown)
            {
                thrown = true;
                throw std::runtime_error{"onDraw failed"};
            }
        }));

    EXPECT_THROW(renderer->submitFrame({}), std::runtime_error);
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->failure());

    EXPECT_NO_THROW(renderer->submitFrame({}));
}

/**
 * A mesh whose layout does not cover the inputs of the shader is a mistake of the renderable, and
 * it is the renderable that is told: RenderQueue::submit() throws the DrawError, with the name the
 * draw gave, the mesh, the shader and the input that is missing, and submitFrame() lets it reach
 * the caller. The frame is not queued and the renderer keeps running: the next frame, with a mesh
 * that fits, is drawn. Renderer::checkDraw() says the same without a frame.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, incompatibleDrawThrowsDrawErrorFromSubmitFrameAndTheRendererRuns)
{
    auto renderer = makeRenderer();
    auto material = makeMaterial(*renderer, "WorldSpace");
    auto bad      = makeMesh(*renderer, gIncompatibleLayout, "quad");
    auto good     = makeMesh(*renderer, gCompatibleLayout, "quad");

    bool first = true;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit(first ? bad : good, material, Math::Matrix4x4f{1.0f});
        }));

    try
    {
        renderer->submitFrame({});
        FAIL() << "submitFrame() did not throw.";
    }
    catch (const DrawError& error)
    {
        EXPECT_EQ(std::string_view{error.what()},
                  "mesh \"quad\" (float2 @0) does not provide location 1 (vec4 inColor) of "
                  "shader \"WorldSpace\"");
    }
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_TRUE(RendererTestAccess::packets(*renderer).empty());

    const auto verdict = renderer->checkDraw(bad, material);
    ASSERT_FALSE(verdict);
    EXPECT_EQ(verdict.error(),
              "mesh \"quad\" (float2 @0) does not provide location 1 (vec4 inColor) of shader "
              "\"WorldSpace\"");
    EXPECT_TRUE(renderer->checkDraw(good, material));
    EXPECT_TRUE(renderer->checkDraw({}, material));
    EXPECT_TRUE(renderer->checkDraw(bad, {}));

    first = false;
    for (int frame = 0; frame < 3; ++frame)
    {
        EXPECT_NO_THROW(renderer->submitFrame({}));
    }
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->failure());
}

/**
 * A failure of the render thread that the renderer did not cause (here a lost surface at the
 * acquire) ends it for good: the state is eFailed and failure() has the exception, every time it is
 * called. The thread keeps taking and dropping packets, so submitFrame() does not wait for it, and
 * there is no way back: the state stays. What is created afterwards is queued and never copied,
 * and its handle, which outlives the renderer, still says it is not resident. A failure that
 * failure() returned is collected, so ~Renderer does not log it.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, renderThreadFailureIsReportedAndFinal)
{
    auto       renderer = makeRenderer();
    const Mesh early    = makeMesh(*renderer, gCompatibleLayout);
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return early.isResident();
        }));

    const SurfaceLoss loss{RendererTestAccess::dispatcher(*renderer)};
    renderer->submitFrame({});

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return renderer->getState() == Renderer::State::eFailed;
        }));
    EXPECT_FALSE(RendererTestAccess::failureCollected(*renderer));

    // The first of these fills the slot, the second waits for a consumer.
    EXPECT_TRUE(returnsInTime(
        [&]
        {
            for (int frame = 0; frame < 3; ++frame)
            {
                renderer->submitFrame({});
            }
        },
        [&]
        {
            // Frees the submits that wait, one at a time: takes what the slot holds, and never
            // waits itself.
            std::stop_source stopped;
            stopped.request_stop();
            for (int frame = 0; frame < 3; ++frame)
            {
                std::ignore = RendererTestAccess::packets(*renderer).pop(stopped.get_token());
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }
        }));

    const std::exception_ptr failure = renderer->failure();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), vk::SurfaceLostKHRError);
    EXPECT_TRUE(RendererTestAccess::failureCollected(*renderer));
    EXPECT_EQ(renderer->failure(), failure);
    EXPECT_EQ(renderer->getState(), Renderer::State::eFailed);

    const Mesh late = makeMesh(*renderer, gCompatibleLayout);
    EXPECT_EQ(renderer->uploads().pendingCount, 1u);
    EXPECT_FALSE(late.isResident());

    renderer.reset();
    EXPECT_TRUE(early.isResident());
    EXPECT_FALSE(late.isResident());
}

/**
 * A renderable may queue another from onDraw(): no lock is held while it runs. The child is
 * adopted by the next submitFrame(), and drawn in that frame.
 */
TEST_F(RendererTest, addRenderableFromOnDrawIsDrawnNextFrame)
{
    std::vector<std::string> log;

    auto renderer = makeRenderer();
    bool added    = false;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& /*queue*/)
        {
            if (!added)
            {
                added = true;
                log.emplace_back("parent.onDraw");
                renderer->addRenderable(std::make_shared<LambdaRenderable>(
                    [&log, logged = false](const FrameContext& /*context*/,
                                           RenderQueue& /*queue*/) mutable
                    {
                        if (!std::exchange(logged, true))
                        {
                            log.emplace_back("child.onDraw");
                        }
                    }));
            }
        }));

    renderer->submitFrame({});
    EXPECT_EQ(log, (std::vector<std::string>{"parent.onDraw"}));

    renderer->submitFrame({});
    EXPECT_EQ(log, (std::vector<std::string>{"parent.onDraw", "child.onDraw"}));
}

/**
 * A draw without a mesh or a material is dropped when it is submitted, and the other draws of the
 * frame are drawn: the valid draw's pipeline is in the table afterwards. A default handle is
 * empty, which is what "without" means, and a created one is not; a copy shares the mesh.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, drawWithoutMeshOrMaterialIsSkipped)
{
    auto renderer = makeRenderer();
    auto material = makeMaterial(*renderer, "WorldSpace");
    auto mesh     = makeMesh(*renderer, gCompatibleLayout);

    EXPECT_FALSE(Mesh{});
    EXPECT_FALSE(Material{});
    EXPECT_NE(makeMesh(*renderer, gCompatibleLayout), mesh);
    ASSERT_TRUE(mesh);
    ASSERT_TRUE(material);
    EXPECT_EQ(Mesh{mesh}, mesh);

    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit({}, {}, Math::Matrix4x4f{1.0f});
            queue.submit(mesh, {}, Math::Matrix4x4f{1.0f});
            queue.submit({}, material, Math::Matrix4x4f{1.0f});
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
        }));
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    RendererTestAccess::join(*renderer);
    EXPECT_FALSE(renderer->failure());

    // The valid draw reached the render thread: it asked for its pipeline.
    EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 1u);
}

/** onDraw() runs on the thread that submits, with the context it was given. */
TEST_F(RendererTest, onDrawRunsOnSubmittingThread)
{
    constexpr double kTime = 1.5;

    std::thread::id drawnOn;
    double          timeSeen = 0.0;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& context, RenderQueue& /*queue*/)
        {
            drawnOn  = std::this_thread::get_id();
            timeSeen = context.timeSeconds;
        }));
    renderer->submitFrame({.timeSeconds = kTime});

    EXPECT_EQ(drawnOn, std::this_thread::get_id());
    EXPECT_EQ(timeSeen, kTime);
}

/**
 * trySubmitFrame() calls no renderable while the render thread still holds the previous frame. A
 * zero-sized window makes the render thread sleep after each frame, which keeps the slot full long
 * enough to see.
 */
TEST_F(RendererTest, trySubmitFrameSkipsWhileQueued)
{
    std::size_t draws = 0u;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&draws](const FrameContext& /*context*/, RenderQueue& /*queue*/)
        {
            ++draws;
        }));

    renderer->resize({});
    std::size_t calls    = 0u;
    const auto  deadline = std::chrono::steady_clock::now() + gDeadline;
    while (calls == draws && std::chrono::steady_clock::now() < deadline)
    {
        renderer->trySubmitFrame({});
        ++calls;
    }
    EXPECT_LT(draws, calls) << "No call found the slot full.";
}

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshRejectsMalformedDescription)
{
    constexpr std::uint32_t      kStride = 8u;
    const std::vector<std::byte> bytes(std::size_t{2u} * kStride);
    const VertexLayout           float2 = vertexLayout(kStride, {{VertexFormat::eFloat2, 0u}});

    auto renderer = makeRenderer();

    EXPECT_THROW(std::ignore = renderer->createMesh({.layout = float2}), std::invalid_argument);
    EXPECT_THROW(
        std::ignore = renderer->createMesh(
            {.vertices = bytes, .layout = vertexLayout(0u, {{VertexFormat::eFloat2, 0u}})}),
        std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createMesh(
                     {.vertices = std::span{bytes}.first(kStride + 1u), .layout = float2}),
                 std::invalid_argument);
    EXPECT_THROW(
        std::ignore = renderer->createMesh(
            {.vertices = bytes, .layout = vertexLayout(kStride, {{VertexFormat::eFloat2, 4u}})}),
        std::invalid_argument);

    // More attributes than a pipeline takes: the helper refuses to make such a layout, and a layout
    // that was filled by hand is refused by the renderer.
    EXPECT_THROW(std::ignore = vertexLayout(kStride, {{}, {}, {}, {}, {}, {}, {}, {}, {}}),
                 std::invalid_argument);
    VertexLayout tooMany   = float2;
    tooMany.attributeCount = static_cast<std::uint8_t>(gMaxVertexAttributes + 1u);
    EXPECT_THROW(std::ignore = renderer->createMesh({.vertices = bytes, .layout = tooMany}),
                 std::invalid_argument);

    // A stride or an offset off a multiple of 4 puts a component at a misaligned address.
    constexpr std::uint32_t      kOddStride = 10u;
    const std::vector<std::byte> oddBytes(std::size_t{2u} * kOddStride);
    EXPECT_THROW(std::ignore = renderer->createMesh(
                     {.vertices = oddBytes,
                      .layout   = vertexLayout(kOddStride, {{VertexFormat::eFloat2, 0u}})}),
                 std::invalid_argument);
    // The attribute fits the stride (2 + 8 <= 12), so only the multiple-of-4 rule rejects it.
    constexpr std::uint32_t      kFitStride = 12u;
    const std::vector<std::byte> fitBytes(std::size_t{2u} * kFitStride);
    EXPECT_THROW(std::ignore = renderer->createMesh(
                     {.vertices = fitBytes,
                      .layout   = vertexLayout(kFitStride, {{VertexFormat::eFloat2, 2u}})}),
                 std::invalid_argument);

    // One vertex whose stride is the device maximum plus 4. Devices with a huge maximum would make
    // the vertex too big to test; lavapipe's is small.
    const std::uint32_t maxStride =
        RendererTestAccess::limits(*renderer).maxVertexInputBindingStride;
    constexpr std::uint32_t kLargestTestable = 1u << 16u;
    if (maxStride <= kLargestTestable)
    {
        const std::vector<std::byte> hugeVertex(std::size_t{maxStride} + 4u);
        EXPECT_THROW(std::ignore = renderer->createMesh(
                         {.vertices = hugeVertex,
                          .layout   = vertexLayout(maxStride + 4u, {{VertexFormat::eFloat2, 0u}})}),
                     std::invalid_argument);
    }

    // The name of the description starts the message.
    try
    {
        std::ignore = renderer->createMesh({.name = "ship.obj", .layout = float2});
        FAIL() << "createMesh() did not throw.";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_TRUE(std::string_view{error.what()}.starts_with("Mesh \"ship.obj\": "));
    }

    const Mesh mesh = renderer->createMesh({.vertices = bytes, .layout = float2});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(HandleTestAccess::data(mesh).vertexCount, 2u);
}

/** Indices are Renderer's to check: without robustBufferAccess an index past the vertices reads
 * outside the buffer. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshRejectsMalformedIndices)
{
    constexpr std::uint32_t      kStride = 8u;
    const std::vector<std::byte> bytes(std::size_t{3u} * kStride);
    const VertexLayout           float2     = vertexLayout(kStride, {{VertexFormat::eFloat2, 0u}});
    const std::uint32_t          triangle[] = {0u, 1u, 2u};
    const std::uint32_t          quad[]     = {0u, 1u, 2u, 0u};
    const std::uint32_t          pastEnd[]  = {0u, 1u, 3u};

    auto renderer = makeRenderer();

    EXPECT_THROW(std::ignore =
                     renderer->createMesh({.vertices = bytes, .indices = quad, .layout = float2}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createMesh(
                     {.vertices = bytes, .indices = pastEnd, .layout = float2}),
                 std::invalid_argument);

    const Mesh indexed =
        renderer->createMesh({.vertices = bytes, .indices = triangle, .layout = float2});
    ASSERT_TRUE(indexed);
    EXPECT_EQ(HandleTestAccess::data(indexed).indexCount, 3u);

    const Mesh plain = renderer->createMesh({.vertices = bytes, .layout = float2});
    ASSERT_TRUE(plain);
    EXPECT_EQ(HandleTestAccess::data(plain).indexCount, 0u);
}

/**
 * Meshes of one layout have one layout id, and a mesh of another layout has another: the tables of
 * the renderer that hold an entry per layout key on it. Ids are not 0, which is none.
 */
TEST_F(RendererTest, meshesOfOneLayoutShareALayoutId)
{
    auto renderer = makeRenderer();

    const Mesh first   = makeMesh(*renderer, gCompatibleLayout);
    const Mesh second  = makeMesh(*renderer, gCompatibleLayout);
    const Mesh another = makeMesh(*renderer, gIncompatibleLayout);

    EXPECT_NE(HandleTestAccess::data(first).layoutId, 0u);
    EXPECT_EQ(HandleTestAccess::data(first).layoutId, HandleTestAccess::data(second).layoutId);
    EXPECT_NE(HandleTestAccess::data(first).layoutId, HandleTestAccess::data(another).layoutId);
}

/**
 * The indices follow the vertices, at a multiple of 4 bytes because the stride is one. The staging
 * buffer holds both. The render thread is held in the submit of another mesh, so that the staging
 * buffer is still in the queue when the test reads it.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshStagesIndicesAfterVertices)
{
    constexpr std::uint32_t kStride = 12u;
    // 36 bytes of vertices: the indices start at 36.
    std::vector<std::byte> vertices(std::size_t{3u} * kStride);
    for (std::size_t i = 0u; i < vertices.size(); ++i)
    {
        vertices[i] = static_cast<std::byte>(i + 1u);
    }
    const std::uint32_t indices[] = {2u, 1u, 0u};

    auto     renderer = makeRenderer();
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    spy.holdNextSubmit();
    const Mesh held = makeMesh(*renderer, gCompatibleLayout);
    ASSERT_TRUE(spy.waitUntilHeld());

    const Mesh mesh =
        renderer->createMesh({.vertices = vertices,
                              .indices  = indices,
                              .layout   = vertexLayout(kStride, {{VertexFormat::eFloat2, 0u}})});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(HandleTestAccess::data(mesh).indexOffset, 36u);

    // The bytes wait in the staging buffer, which the render thread copies into the buffer of
    // the mesh. Reading it back through mapForWrite() is meant for writing; it is legal
    // on lavapipe, whose host memory is coherent.
    const auto staging = RendererTestAccess::arrivedStaging(*renderer, 0u).lock();
    ASSERT_TRUE(staging);
    EXPECT_EQ(staging->byteSize(), HandleTestAccess::data(mesh).indexOffset + sizeof(indices));
    EXPECT_EQ(HandleTestAccess::data(mesh).buffer->byteSize(), staging->byteSize());
    const auto mapping = staging->mapForWrite();
    ASSERT_GE(mapping.bytes().size(), HandleTestAccess::data(mesh).indexOffset + sizeof(indices));
    EXPECT_EQ(std::memcmp(mapping.bytes().data(), vertices.data(), vertices.size()), 0);
    EXPECT_EQ(std::memcmp(mapping.bytes().data() + HandleTestAccess::data(mesh).indexOffset,
                          indices,
                          sizeof(indices)),
              0);

    spy.release();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return mesh.isResident();
        }));
}

/** An indexed mesh reaches the render thread; the validation layer checks the draw. */
TEST_F(RendererTest, indexedMeshIsDrawn)
{
    const std::uint32_t indices[] = {0u, 1u, 2u};

    auto renderer = makeRenderer();
    auto mesh =
        renderer->createMesh({.vertices = std::vector<std::byte>(std::size_t{3u} * gQuadStride),
                              .indices  = indices,
                              .layout   = gCompatibleLayout});
    renderer->addRenderable(drawing(mesh, makeMaterial(*renderer, "WorldSpace")));
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    RendererTestAccess::join(*renderer);
    EXPECT_FALSE(renderer->failure());
    EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 1u);
}

/**
 * A draw does not bind what the draw before it bound in the same frame. One frame submits, in
 * order: mesh A (indexed) twice, A with another program, a non-indexed mesh B, A again, and an
 * indexed mesh C. The renderer records them sorted by pipeline, material and mesh: A, A with the
 * first program; then A, A, B, C with the other one. Binds that reach the driver, with the reason
 * for each:
 *   pipeline 2: the first draw, and the first of the other program;
 *   vertex buffers 3: A, B, C (the program changes between A and A, which is no reason to bind it
 *   again);
 *   index buffers 2: A, and C. B draws without indices and leaves the bound index buffer as it
 *   was.
 * Without the skip it is 6, 6 and 5.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, repeatedBindsAreSkipped)
{
    const std::uint32_t indices[] = {0u, 1u, 2u};
    const auto          vertices  = std::vector<std::byte>(std::size_t{3u} * gQuadStride);

    auto renderer = makeRenderer();
    auto indexed  = [&]
    {
        return renderer->createMesh(
            {.vertices = vertices, .indices = indices, .layout = gCompatibleLayout});
    };
    const Mesh     meshA = indexed();
    const Mesh     meshB = makeMesh(*renderer, gCompatibleLayout);
    const Mesh     meshC = indexed();
    const Material first = makeMaterial(*renderer, "WorldSpace");
    const Material other = makeMaterial(*renderer, "ScreenSpacePositionColor");

    // Only the first frame draws, so the counts are those of one frame.
    bool drawn = false;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            if (std::exchange(drawn, true))
            {
                return;
            }
            const Math::Matrix4x4f world{1.0f};
            queue.submit(meshA, first, world);
            queue.submit(meshA, first, world);
            queue.submit(meshA, other, world);
            queue.submit(meshB, other, world);
            queue.submit(meshA, other, world);
            queue.submit(meshC, other, world);
        }));

    for (const Mesh& mesh : {meshA, meshB, meshC})
    {
        ASSERT_TRUE(becomesTrue(
            [&]
            {
                return mesh.isResident();
            }));
    }

    {
        // The render thread is idle: nothing is left to copy, and no frame is submitted.
        const BindCounter counter{RendererTestAccess::dispatcher(*renderer)};
        renderer->submitFrame({});
        renderer->submitFrame({});
        RendererTestAccess::join(*renderer);
        EXPECT_FALSE(renderer->failure());
        EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 2u);
        EXPECT_EQ(gPipelineBinds, 2);
        EXPECT_EQ(gVertexBufferBinds, 3);
        EXPECT_EQ(gIndexBufferBinds, 2);
    }
}

/**
 * A texture is a handle: empty by default, equal to its copies, and to no other texture. Material
 * parameters compare by it, so a texture that compared equal to another would make two materials
 * of different textures equal.
 */
TEST_F(RendererTest, textureIsAHandleThatComparesByIdentity)
{
    auto renderer = makeRenderer();

    const Texture texture = makeTexture(*renderer);
    EXPECT_FALSE(Texture{});
    EXPECT_TRUE(texture);
    EXPECT_EQ(Texture{texture}, texture);
    EXPECT_NE(makeTexture(*renderer), texture);
}

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createTextureRejectsMalformedDescription)
{
    auto renderer = makeRenderer();

    // The render thread is held in the submit of the first valid texture, so that what is queued
    // can be counted at the end.
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};
    spy.holdNextSubmit();

    // The handles are kept: a texture that nobody holds is dropped from the queue.
    const std::vector<std::byte> pixels(std::size_t{2u} * 3u * gPixelSize);
    std::vector<Texture>         valid;
    EXPECT_NO_THROW(valid.push_back(
        renderer->createTexture({.pixels = pixels, .extent = {.width = 2u, .height = 3u}})));
    EXPECT_NO_THROW(
        valid.push_back(renderer->createTexture({.pixels = pixels,
                                                 .extent = {.width = 2u, .height = 3u},
                                                 .format = TextureFormat::eRGBA8Unorm})));

    EXPECT_THROW(std::ignore = renderer->createTexture({.extent = {.width = 2u, .height = 3u}}),
                 std::invalid_argument);
    // No pixels for no extent are still no texture.
    EXPECT_THROW(std::ignore = renderer->createTexture({}), std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = pixels, .extent = {.width = 0u, .height = 3u}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = pixels, .extent = {.width = 2u, .height = 0u}}),
                 std::invalid_argument);
    // One past the device limit: the pixels fit the byte count, the image does not exist.
    const std::uint32_t tooWide = RendererTestAccess::limits(*renderer).maxImageDimension2D + 1u;
    const std::vector<std::byte> tooWidePixels(std::size_t{tooWide} * gPixelSize);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = tooWidePixels, .extent = {.width = tooWide, .height = 1u}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = tooWidePixels, .extent = {.width = 1u, .height = tooWide}}),
                 std::invalid_argument);
#ifndef _WIN32
    // A square over the limit whose pixels have the exact byte count (4 GiB and a bit). The extent
    // must be refused before anything reads the pixels or makes the staging buffer. The pixels are
    // address space with no access rights, so any read of them faults, and the test costs no
    // memory.
    constexpr std::uint32_t kHugeSide = 32769u;
    if (RendererTestAccess::limits(*renderer).maxImageDimension2D < kHugeSide)
    {
        const InaccessibleBytes huge{std::size_t{kHugeSide} * kHugeSide * gPixelSize};
        ASSERT_NE(huge.data(), nullptr);
        EXPECT_THROW(
            std::ignore = renderer->createTexture(
                {.pixels = huge.bytes(), .extent = {.width = kHugeSide, .height = kHugeSide}}),
            std::invalid_argument);
    }
#endif
    // One byte too few, one byte too many, and the right size for the other extent.
    EXPECT_THROW(std::ignore =
                     renderer->createTexture({.pixels = std::span{pixels}.first(pixels.size() - 1u),
                                              .extent = {.width = 2u, .height = 3u}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = pixels, .extent = {.width = 2u, .height = 2u}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = pixels, .extent = {.width = 6u, .height = 6u}}),
                 std::invalid_argument);

    // The name of the description starts the message.
    try
    {
        std::ignore = renderer->createTexture(
            {.name = "court.png", .pixels = pixels, .extent = {.width = 2u, .height = 2u}});
        FAIL() << "createTexture() did not throw.";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_TRUE(std::string_view{error.what()}.starts_with("Texture \"court.png\": "));
    }

    // Only the two valid calls left an upload.
    EXPECT_EQ(renderer->uploads().pendingCount, 2u);

    spy.release();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return renderer->uploads().pendingCount == 0u;
        }));
}

/**
 * A frame in flight keeps the mesh's buffer alive after the renderer let go of the mesh: the frame
 * that drew it is rendered and gone, but its slot is not reused yet. Once the slot is reused,
 * nothing holds the buffer. The copy of the mesh was submitted before, and its command buffer let
 * go of the buffer when the ring of the upload queue reused the pool of that copy, so only the
 * draw's recording holds the buffer: the copy would otherwise hide a missing retention of the draw.
 *
 * The packet of a frame dies on the render thread after the frame, which lets go of the mesh, so
 * the test waits for the handle to expire; that is when the frame was recorded. The test submits
 * no third frame after the draw, and reusing the slot of the draw takes one (the renderer has two
 * frame slots).
 *
 * It proves that Engine records the buffer through the retaining path and the renderer holds
 * nothing on top. GPU safety rests on the Graphics retention tests; a validation error fails the
 * test in TearDown().
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, meshBufferOutlivesMeshWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    Mesh mesh       = makeMesh(*renderer, gCompatibleLayout);
    auto weakMesh   = HandleTestAccess::weak(mesh);
    auto weakBuffer = std::weak_ptr<Graphics::Vulkan::Buffer>{HandleTestAccess::data(mesh).buffer};

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return mesh.isResident();
        }));
    releaseTheUploadRing(*renderer);
    ASSERT_EQ(HandleTestAccess::data(mesh).buffer.use_count(), 1);

    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [mesh = std::move(mesh), material = makeMaterial(*renderer, "WorldSpace")](
            const FrameContext& /*context*/, RenderQueue& queue) mutable
        {
            // The first frame draws the mesh and lets go of it; later frames draw nothing.
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
            mesh = {};
        }));
    mesh = {};
    renderer->submitFrame({});
    renderer->submitFrame({});

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return weakMesh.expired();
        }));
    EXPECT_FALSE(weakBuffer.expired());

    // Frames that draw nothing cycle through the slots until the draw's slot is reused; the
    // deadline keeps a leak from hanging the test.
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakBuffer.expired();
        }));
}

/**
 * A frame in flight keeps the image of a texture alive after the renderer let go of the material:
 * the draw recorded the texture, and the frame's slot is not reused yet. The copy of the image went
 * before, and let go when the ring reused its pool, so only the draw's recording holds the image.
 *
 * It proves that the draw loop records the texture of a material (useResource) and the renderer
 * holds nothing on top. GPU safety rests on the Graphics retention tests.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, textureOutlivesMaterialWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    Texture texture     = makeTexture(*renderer);
    auto    weakTexture = HandleTestAccess::weak(texture);
    auto    weakImage =
        std::weak_ptr<Graphics::Vulkan::Texture>{HandleTestAccess::data(texture).texture};

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return texture.isResident();
        }));
    releaseTheUploadRing(*renderer);
    ASSERT_EQ(HandleTestAccess::data(texture).texture.use_count(), 1);

    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [mesh     = makeTexturedMesh(*renderer),
         material = makeTexturedMaterial(*renderer, std::move(texture))](
            const FrameContext& /*context*/, RenderQueue& queue) mutable
        {
            // The first frame draws the mesh and lets go of it and the material; later frames
            // draw nothing.
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
            mesh     = {};
            material = {};
        }));
    renderer->submitFrame({});
    renderer->submitFrame({});

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return weakTexture.expired();
        }));
    EXPECT_FALSE(weakImage.expired());

    // Frames that draw nothing cycle through the slots until the draw's slot is reused.
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakImage.expired();
        }));
}

/** As for the texture, for the parameter block of a material. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, parameterBlockOutlivesMaterialWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    std::weak_ptr<Graphics::Vulkan::HeapBuffer> weakBlock;
    std::weak_ptr<const Detail::MaterialData>   weakMaterial;
    {
        Material material = makeMaterial(*renderer, "WorldSpace");
        weakBlock         = HandleTestAccess::data(material).block;
        weakMaterial      = HandleTestAccess::weak(material);
        auto mesh         = makeMesh(*renderer, gCompatibleLayout);
        ASSERT_TRUE(becomesTrue(
            [&]
            {
                return mesh.isResident();
            }));
        renderer->addRenderable(std::make_shared<LambdaRenderable>(
            [mesh = std::move(mesh), material = std::move(material)](
                const FrameContext& /*context*/, RenderQueue& queue) mutable
            {
                queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
                mesh     = {};
                material = {};
            }));
    }

    renderer->submitFrame({});
    renderer->submitFrame({});

    // The packet of the frame let go of the material, and the recording holds the block.
    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return weakMaterial.expired();
        }));
    EXPECT_FALSE(weakBlock.expired());

    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakBlock.expired();
        }));
}

/**
 * A minimized window drops every frame before it records anything, and its uploads go through
 * all the same: they are the render thread's own work, before the frame and whatever becomes of
 * it.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, uploadsGoThroughDroppedFrames)
{
    auto renderer = makeRenderer();

    renderer->resize({});
    auto texture = makeTexture(*renderer);
    auto mesh    = makeMesh(*renderer, gCompatibleLayout);
    renderer->submitFrame({});
    renderer->submitFrame({});

    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return mesh.isResident() && texture.isResident();
        }));
    EXPECT_EQ(renderer->uploads().pendingCount, 0u);
}

/**
 * The staging buffer of a texture and of a mesh waits in the upload queue until a transfer submit
 * took it, then in the command buffer of that submit, and is gone when the ring reuses the pool of
 * the command buffer: two submits later. The render thread is held in the submit of a first mesh,
 * so that the staging buffers can be taken from the queue.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, uploadsAreRecordedAndStagingReleased)
{
    auto     renderer = makeRenderer();
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    spy.holdNextSubmit();
    const Mesh held = makeMesh(*renderer, gCompatibleLayout);
    ASSERT_TRUE(spy.waitUntilHeld());

    auto texture        = makeTexture(*renderer);
    auto mesh           = makeMesh(*renderer, gCompatibleLayout);
    auto textureStaging = RendererTestAccess::arrivedStaging(*renderer, 0u);
    auto meshStaging    = RendererTestAccess::arrivedStaging(*renderer, 1u);
    EXPECT_FALSE(textureStaging.expired());
    EXPECT_FALSE(meshStaging.expired());

    spy.release();
    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return texture.isResident() && mesh.isResident();
        }));
    // Submitted, and held by the command buffer of the submit.
    EXPECT_FALSE(textureStaging.expired());
    EXPECT_FALSE(meshStaging.expired());

    releaseTheUploadRing(*renderer);
    EXPECT_TRUE(textureStaging.expired());
    EXPECT_TRUE(meshStaging.expired());
}

/**
 * A mesh and a texture are copied while no frame is submitted at all, the way a window that is
 * hidden never acquires an image and still has to stream: the render thread wakes for the upload
 * and drains it. isResident() says false while the submit of the copy has not returned (the
 * render thread is held in it) and true after, from the semaphore, with no help of a frame.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, isResidentFlipsAfterAnIdleDrainWithNoFrame)
{
    auto     renderer = makeRenderer();
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    EXPECT_FALSE(Mesh{}.isResident());
    EXPECT_FALSE(Texture{}.isResident());

    spy.holdNextSubmit();
    const Mesh    mesh    = makeMesh(*renderer, gCompatibleLayout);
    const Texture texture = makeTexture(*renderer);
    ASSERT_TRUE(spy.waitUntilHeld());
    EXPECT_FALSE(mesh.isResident());
    EXPECT_FALSE(texture.isResident());

    spy.release();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return mesh.isResident() && texture.isResident();
        }));
    EXPECT_TRUE(RendererTestAccess::packets(*renderer).empty());
}

/**
 * Three meshes wait in the queue, and the budget has room for one: no iteration records more than
 * one of them while no frame wants them. A frame that draws two of them gets both, in the iteration
 * that renders it, over the budget, together with the one of the background that the budget lets
 * through. The render thread is held in the submit of a first mesh while the queue is filled, and
 * the copies of every submit to the transfer queue are read from the spy.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, backgroundUploadsAreBoundedAndTheOnesAFrameDrawsAreNot)
{
    Renderer::Config config              = makeConfig();
    config.backgroundUploadBytesPerFrame = gMeshBytes;

    auto     renderer = makeRenderer(config);
    auto     material = makeMaterial(*renderer, "WorldSpace");
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    spy.holdNextSubmit();
    const Mesh held = makeMesh(*renderer, gCompatibleLayout);
    ASSERT_TRUE(spy.waitUntilHeld());

    // In the order of arrival: a background mesh, the two that the frame draws, a background mesh.
    const Mesh background1 = makeMesh(*renderer, gCompatibleLayout);
    const Mesh drawn1      = makeMesh(*renderer, gCompatibleLayout);
    const Mesh drawn2      = makeMesh(*renderer, gCompatibleLayout);
    const Mesh background2 = makeMesh(*renderer, gCompatibleLayout);
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit(drawn1, material, Math::Matrix4x4f{1.0f});
            queue.submit(drawn2, material, Math::Matrix4x4f{1.0f});
        }));
    renderer->submitFrame({});

    spy.release();
    for (const Mesh& mesh : {held, background1, drawn1, drawn2, background2})
    {
        ASSERT_TRUE(becomesTrue(
            [&]
            {
                return mesh.isResident();
            }));
    }

    // The held mesh alone; then the iteration of the frame: the two it draws and the first of the
    // background; then the last of the background, alone.
    EXPECT_EQ(spy.copiesPerUpload(), (std::vector<std::size_t>{1u, 3u, 1u}));
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
}

/**
 * uploads() counts what is queued and not yet submitted, and counts down to nothing; a mesh whose
 * handles were all dropped before the render thread took it is dropped from the queue and never
 * copied. The render thread is held in the submit of a first mesh, so that the queue is full when
 * it takes the rest.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, uploadsCountDownAndADroppedMeshIsNeverCopied)
{
    auto     renderer = makeRenderer();
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    EXPECT_EQ(renderer->uploads().pendingCount, 0u);
    EXPECT_EQ(renderer->uploads().pendingBytes, 0u);

    spy.holdNextSubmit();
    const Mesh held = makeMesh(*renderer, gCompatibleLayout);
    ASSERT_TRUE(spy.waitUntilHeld());
    // Taken and recorded, but the submit has not returned.
    EXPECT_EQ(renderer->uploads().pendingCount, 1u);

    Mesh       dropped = makeMesh(*renderer, gCompatibleLayout);
    const Mesh kept    = makeMesh(*renderer, gCompatibleLayout);
    EXPECT_EQ(renderer->uploads().pendingCount, 3u);
    EXPECT_EQ(renderer->uploads().pendingBytes, 3u * gMeshBytes);

    // Every handle of it is gone before the render thread looks at the queue again.
    dropped = {};

    spy.release();
    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return held.isResident() && kept.isResident();
        }));
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return renderer->uploads().pendingCount == 0u;
        }));
    EXPECT_EQ(renderer->uploads().pendingBytes, 0u);

    // One copy for the held mesh and one for the kept one: the dropped mesh was not copied.
    EXPECT_EQ(spy.copiesPerUpload(), (std::vector<std::size_t>{1u, 1u}));
}

/**
 * A trickle of background uploads does not take the frame slots: every upload-only iteration used
 * no slot, so the frames alternate between the two slots as they would without it. An iteration
 * that took a slot would wait for the presentation of the slot's last frame and move the frames to
 * one slot. Each round submits a frame, waits until the render thread has taken it, and creates a
 * mesh, which the thread then copies in an iteration of its own, without a packet. The slot of a
 * frame is the binary semaphore its submit signals, which the spy reads.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, framesInFlightStayTwoWhileBackgroundUploadsTrickle)
{
    constexpr int kRounds = 8;

    // The default of the config is the two slots that this test relies on.
    EXPECT_EQ(Renderer::Config{}.framesInFlight, 2u);

    auto     renderer = makeRenderer();
    QueueSpy spy{RendererTestAccess::dispatcher(*renderer)};

    std::vector<Mesh> meshes;
    for (int round = 0; round < kRounds; ++round)
    {
        renderer->submitFrame({});
        ASSERT_TRUE(becomesTrue(
            [&]
            {
                return RendererTestAccess::packets(*renderer).empty();
            }));
        meshes.push_back(makeMesh(*renderer, gCompatibleLayout));
        ASSERT_TRUE(becomesTrue(
            [&]
            {
                return meshes.back().isResident();
            }));
    }
    RendererTestAccess::join(*renderer);

    std::vector<VkSemaphore> slots;
    for (const SpiedSubmit& submit : spy.submits())
    {
        if (submit.signal != VK_NULL_HANDLE)
        {
            slots.push_back(submit.signal);
        }
    }
    ASSERT_GE(slots.size(), std::size_t{kRounds} - 1u);

    std::set<VkSemaphore> distinct{slots.begin(), slots.end()};
    EXPECT_EQ(distinct.size(), 2u);
    for (std::size_t frame = 1u; frame < slots.size(); ++frame)
    {
        EXPECT_NE(slots[frame], slots[frame - 1u]) << "frames " << frame - 1u << " and " << frame;
    }
}

/**
 * A mesh handle outlives the Renderer and still answers isResident(): the ticket holds the
 * timeline semaphore, which the device keeps, so the answer comes from the semaphore with the
 * renderer gone.
 */
TEST_F(RendererTest, handleOutlivingTheRendererStillAnswersIsResident)
{
    auto    renderer = makeRenderer();
    Mesh    mesh     = makeMesh(*renderer, gCompatibleLayout);
    Texture texture  = makeTexture(*renderer);
    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return mesh.isResident() && texture.isResident();
        }));

    renderer.reset();

    EXPECT_TRUE(mesh.isResident());
    EXPECT_TRUE(texture.isResident());
}

/**
 * The buffers of the Frame block take their heap slots when the Renderer is made, so a heap that
 * materials fill fails createMaterial(), on the thread that asks, and never the render thread at
 * its first frame. With room for the frame blocks and one more, the first material fits, the
 * second does not, and the frames that draw the first are rendered. With no room for the frame
 * blocks, the constructor fails.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, frameBlocksTakeTheirHeapSlotsAtConstruction)
{
    constexpr std::uint32_t kFrameSlots = 2u;

    Renderer::Config config = makeConfig();
    config.framesInFlight   = kFrameSlots;
    config.resourceSlots    = kFrameSlots - 1u;
    EXPECT_THROW(std::ignore = makeRenderer(config), std::runtime_error);

    config.resourceSlots = kFrameSlots + 1u;
    auto renderer        = makeRenderer(config);
    ASSERT_EQ(RendererTestAccess::frameBlocks(*renderer).size(), std::size_t{kFrameSlots});

    const Material material = makeMaterial(*renderer, "WorldSpace");
    EXPECT_THROW(std::ignore = makeMaterial(*renderer, "WorldSpace"), std::runtime_error);

    renderer->addRenderable(drawing(makeMesh(*renderer, gCompatibleLayout), material));
    for (int frame = 0; frame < 4; ++frame)
    {
        renderer->submitFrame({});
    }
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->failure());
}

/**
 * Another thread creates and drops textures and materials while the main thread submits frames
 * that draw them. The creator ends without an exception, nothing deadlocks (a hang here ends at
 * the test timeout), and the queue of uploads drains.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createTextureWhileRendering)
{
    constexpr int kTextures = 64;

    auto renderer = makeRenderer();
    auto mesh     = makeTexturedMesh(*renderer);

    std::mutex currentMutex;
    Material   current;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            Material material;
            {
                const std::scoped_lock lock{currentMutex};
                material = current;
            }
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
        }));

    std::atomic<bool>  done{false};
    std::exception_ptr failure;
    std::thread        creator{[&]
                               {
                            try
                            {
                                for (int i = 0; i < kTextures; ++i)
                                {
                                    Material material =
                                        makeTexturedMaterial(*renderer, makeTexture(*renderer));
                                    const std::scoped_lock lock{currentMutex};
                                    current = std::move(material);
                                }
                            }
                            catch (...)
                            {
                                failure = std::current_exception();
                            }
                            done = true;
                               }};
    while (!done)
    {
        renderer->submitFrame({});
    }
    creator.join();
    EXPECT_FALSE(failure);

    {
        const std::scoped_lock lock{currentMutex};
        current = {};
    }
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return renderer->uploads().pendingCount == 0u;
        }));
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->failure());
}

/**
 * resize() takes effect before the next frame is rendered: the swapchain has the new extent once
 * the first packet after the request has been taken. A second frame is submitted so that the first
 * has been taken, and the thread is joined before the extent is read.
 */
TEST_F(RendererTest, resizeRecreatesAtRequestedExtent)
{
    for (const std::uint32_t size : {gResizedExtent, gResizedAgainExtent})
    {
        auto renderer = makeRenderer();

        renderer->resize({.width = size, .height = size});
        renderer->submitFrame({});
        renderer->submitFrame({});
        RendererTestAccess::join(*renderer);

        EXPECT_FALSE(renderer->failure());
        EXPECT_EQ(RendererTestAccess::swapchainExtent(*renderer),
                  (Extent{.width = size, .height = size}));
    }
}

// NOLINTBEGIN(readability-magic-numbers): the numbers are sizes and distances of the scenes.
namespace
{
/** RGBA, one byte each: what DrawRecording draws into a target of this format. */
constexpr vk::Format gReadbackFormat = vk::Format::eR8G8B8A8Unorm;

/**
 * Draws @p packet the way the render thread does (the Frame block, the push data, the pipelines,
 * the resources), but into a color target of @p size by @p size pixels instead of a swapchain
 * image, which no test can read, and returns its pixels, rows from the top. It waits until the
 * meshes and the textures of the draws have been copied by the render thread, which then has
 * nothing to do, so the two never use the queue at the same time. The pipelines come from a table
 * of the harness, not from the one of the render thread.
 */
std::vector<std::byte>
drawToPixels(Renderer& renderer, const FramePacket& packet, std::uint32_t size)
{
    using namespace Graphics::Vulkan;

    for (const auto& draw : packet.draws)
    {
        EXPECT_TRUE(becomesTrue(
            [&]
            {
                return draw.mesh.isResident();
            }));
        for (const Engine::Texture& texture : HandleTestAccess::data(draw.material).textures)
        {
            EXPECT_TRUE(becomesTrue(
                [&]
                {
                    return texture.isResident();
                }));
        }
    }

    const DevicePtr           device    = RendererTestAccess::device(renderer);
    const auto                queue     = device->getGraphicsQueue();
    const auto                heap      = RendererTestAccess::heap(renderer);
    const auto                fencePool = FencePool::create(device);
    const auto                pool      = device->createCommandBufferPool();
    const vk::Extent2D        extent{.width = size, .height = size};
    PipelineTable             pipelines{device};
    std::vector<ResolvedDraw> resolved;
    const vk::Format          depthFormat = RendererTestAccess::depthFormat(renderer);
    DrawRecording::resolve(packet, pipelines, gReadbackFormat, depthFormat, resolved);

    const RenderTargetPtr target = device->createRenderTarget(
        extent,
        gReadbackFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
    const RenderTargetPtr depthTarget = device->createRenderTarget(
        extent, depthFormat, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    const HeapBufferPtr frameBlock = device->createHeapBuffer(heap, sizeof(FrameBlock));
    DrawRecording::writeFrameBlock(*frameBlock, packet, extent);

    const vk::DeviceSize bytes = vk::DeviceSize{size} * size * 4u;
    const BufferPtr      readback =
        device->createBuffer({.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferDst},
                             {.flags = AllocationFlag::eHostRandomAccess});

    constexpr vk::ImageSubresourceRange kColor{
        .aspectMask = vk::ImageAspectFlagBits::eColor, .levelCount = 1u, .layerCount = 1u};

    auto recorder = pool->record();
    recorder.bindDescriptorHeap(heap);
    recorder.setImageBarrier(target,
                             {.srcStageMask  = vk::PipelineStageFlagBits2::eTopOfPipe,
                              .dstStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                              .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                              .oldLayout     = vk::ImageLayout::eUndefined,
                              .newLayout     = vk::ImageLayout::eColorAttachmentOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = kColor});
    {
        auto depth = DrawRecording::prepareDepth(recorder, depthTarget);
        auto pass  = recorder.beginRendering({{.attachment = target,
                                               .loadOp     = vk::AttachmentLoadOp::eClear,
                                               .storeOp    = vk::AttachmentStoreOp::eStore,
                                               .clearValue = DrawRecording::clearColor(packet)}},
                                             depth);
        DrawRecording::recordDraws(pass, packet, resolved, pipelines, frameBlock, extent);
    }
    recorder.setImageBarrier(target,
                             {.srcStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                              .srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                              .dstStageMask  = vk::PipelineStageFlagBits2::eCopy,
                              .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
                              .oldLayout     = vk::ImageLayout::eColorAttachmentOptimal,
                              .newLayout     = vk::ImageLayout::eTransferSrcOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = kColor});
    recorder.copyImageToBuffer(target, readback);
    recorder.setBufferBarrier(readback,
                              {.srcStageMask        = vk::PipelineStageFlagBits2::eCopy,
                               .srcAccessMask       = vk::AccessFlagBits2::eTransferWrite,
                               .dstStageMask        = vk::PipelineStageFlagBits2::eHost,
                               .dstAccessMask       = vk::AccessFlagBits2::eHostRead,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .offset              = 0u,
                               .size                = VK_WHOLE_SIZE});
    auto executable = recorder.finish();
    queue->submit(*fencePool, {.commandBuffers = std::span{&executable, 1u}});
    // Waits the fence of the submit.
    pool->reset();

    const auto mapping = readback->mapForRead();
    const auto pixels  = mapping.bytes().first(bytes);

    return {pixels.begin(), pixels.end()};
}

/**
 * The quad of the example: a square of side 2 * @p half around the origin in the plane z = 0, two
 * floats of position and two of uv per vertex, drawn through indices. It goes bottom left, top
 * left, top right, which is clockwise for a viewer, and its uv row 0 is at the top. With
 * @p reversed its indices run the other way round.
 */
Mesh
makeQuad(Renderer& renderer, float half, bool reversed = false)
{
    const std::array<float, 16>        vertices{-half,
                                                -half,
                                                0.0f,
                                                1.0f, // bottom left
                                                -half,
                                                half,
                                                0.0f,
                                                0.0f, // top left
                                                half,
                                                half,
                                                1.0f,
                                                0.0f, // top right
                                                half,
                                                -half,
                                                1.0f,
                                                1.0f}; // bottom right
    const std::array<std::uint32_t, 6> forward{0u, 1u, 2u, 2u, 3u, 0u};
    const std::array<std::uint32_t, 6> backward{2u, 1u, 0u, 0u, 3u, 2u};

    return renderer.createMesh({.name     = reversed ? "reversed quad" : "quad",
                                .vertices = std::as_bytes(std::span{vertices}),
                                .indices  = reversed ? backward : forward,
                                .layout   = gTexturedLayout});
}

/** A position of three floats and a color of four: what the WorldSpace shader reads. */
constexpr VertexLayout gColoredLayout = vertexLayout(
    7u * sizeof(float),
    {{VertexFormat::eFloat3, 0u}, {VertexFormat::eFloat4, sizeOf(VertexFormat::eFloat3)}});

/** What makeColoredQuad() makes. */
struct ColoredQuad
{
    /** Half the side. */
    float half;
    /** The z of its plane. */
    float          depth;
    Math::Vector4f color;
};

/** A square in the plane z = quad.depth, centered on the axis, every vertex of the same color;
 *  clockwise for a viewer, like makeQuad(). */
Mesh
makeColoredQuad(Renderer& renderer, const ColoredQuad& quad)
{
    const auto& [half, depth, color] = quad;
    std::vector<float> vertices;
    vertices.reserve(std::size_t{4u} * 7u);
    for (const auto [across, height] : {std::pair{-half, -half},
                                        std::pair{-half, half},
                                        std::pair{half, half},
                                        std::pair{half, -half}})
    {
        vertices.insert(vertices.end(),
                        {across, height, depth, color.r, color.g, color.b, color.a});
    }
    const std::array<std::uint32_t, 6> indices{0u, 1u, 2u, 2u, 3u, 0u};

    return renderer.createMesh({.name     = "colored quad",
                                .vertices = std::as_bytes(std::span{vertices}),
                                .indices  = indices,
                                .layout   = gColoredLayout});
}

/** A camera at the distance of 20 before the plane z = 0, showing @p height world units from the
 *  bottom to the top. */
CameraView
cameraBeforeThePlane(float height = 10.0f)
{
    constexpr float kDistance = 20.0f;

    return CameraView{.position   = {0.0f, 0.0f, -kDistance},
                      .projection = Orthographic{.height = height}};
}

/** The brightest value of a color channel. */
constexpr std::uint8_t gFull = 255u;

/** The four channels of the pixel at column @p x and row @p y (rows from the top) of the square
 *  image @p pixels of @p size pixels a side. */
std::array<int, 4>
pixelAt(const std::vector<std::byte>& pixels,
        std::uint32_t                 size,
        std::uint32_t                 column,
        std::uint32_t                 row)
{
    const std::size_t first = ((std::size_t{row} * size) + column) * 4u;

    return {std::to_integer<int>(pixels[first]),
            std::to_integer<int>(pixels[first + 1u]),
            std::to_integer<int>(pixels[first + 2u]),
            std::to_integer<int>(pixels[first + 3u])};
}

/** Whether every channel of @p actual is within one step of @p expected: a blend rounds either
 *  way. */
bool
nearlyEqual(const std::array<int, 4>& actual, const std::array<int, 4>& expected)
{
    for (std::size_t channel = 0u; channel < actual.size(); ++channel)
    {
        if (std::abs(actual[channel] - expected[channel]) > 1)
        {
            return false;
        }
    }

    return true;
}

/** Whether the pixel @p index of @p pixels has these colors. */
bool
pixelIs(const std::vector<std::byte>& pixels,
        std::size_t                   index,
        std::uint8_t                  red,
        std::uint8_t                  green,
        std::uint8_t                  blue)
{
    return pixels[(index * 4u) + 0u] == std::byte{red} &&
           pixels[(index * 4u) + 1u] == std::byte{green} &&
           pixels[(index * 4u) + 2u] == std::byte{blue};
}
} // namespace

/**
 * A texture is drawn with the sampler its filter names: the material packs the filter as the
 * sampler index of its TextureRef and the shader reads the sampler by it. A 2x2 checkerboard,
 * magnified, has only its two colors with the nearest filter, and the blend between them with the
 * linear one. Without the sampler index in the TextureRef both would be the same picture.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, textureFilterSelectsTheSamplerOfTheDraw)
{
    constexpr std::uint32_t kSize = 32u;
    constexpr float         kHalf = static_cast<float>(kSize) / 2.0f;

    auto renderer = makeRenderer();

    const std::array<std::uint8_t, 16> checkerboard{
        255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255, 255};
    const auto draw = [&](Filter filter)
    {
        const Texture texture =
            renderer->createTexture({.pixels = std::as_bytes(std::span{checkerboard}),
                                     .extent = {.width = 2u, .height = 2u},
                                     .filter = filter});
        FramePacket packet{.camera = cameraBeforeThePlane(2.0f * kHalf)};
        RenderQueueTestAccess::addDraw(packet,
                                       makeQuad(*renderer, kHalf),
                                       makeTexturedMaterial(*renderer, texture),
                                       Math::Matrix4x4f{1.0f});

        return drawToPixels(*renderer, packet, kSize);
    };

    const std::vector<std::byte> nearest = draw(Filter::eNearest);
    const std::vector<std::byte> linear  = draw(Filter::eLinear);

    std::size_t nearestBlend = 0u;
    std::size_t linearBlend  = 0u;
    std::size_t light        = 0u;
    for (std::size_t pixel = 0u; pixel < std::size_t{kSize} * kSize; ++pixel)
    {
        const bool white = pixelIs(nearest, pixel, gFull, gFull, gFull);
        const bool black = pixelIs(nearest, pixel, 0u, 0u, 0u);
        nearestBlend += (white || black) ? 0u : 1u;
        light += white ? 1u : 0u;
        linearBlend +=
            (pixelIs(linear, pixel, gFull, gFull, gFull) || pixelIs(linear, pixel, 0u, 0u, 0u))
                ? 0u
                : 1u;
    }

    // The quad covers the target, and half of it is light.
    EXPECT_EQ(nearestBlend, 0u);
    EXPECT_EQ(light, std::size_t{kSize} * kSize / 2u);
    EXPECT_GT(linearBlend, 0u);
    EXPECT_NE(nearest, linear);
}

/**
 * The render thread writes the Frame block of every frame into the buffer of the frame's slot: with
 * two frames in flight there are two buffers, with two heap indices, and each holds what its last
 * frame saw: the time and the time between frames of the packet, the size of the swapchain image,
 * and the matrices and the position of the camera. The camera is one for which a block that swaps
 * the view and the projection differs from the right one.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, renderedFramesWriteTheFrameBlockOfTheirSlot)
{
    constexpr int kFrames = 6;

    // Two frame slots, as the test counts them below.
    Renderer::Config config = makeConfig();
    config.framesInFlight   = 2u;

    auto             renderer = makeRenderer(config);
    const CameraView camera{.position = {10.0f, 20.0f, -30.0f}};
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [camera](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.setCamera(camera);
        }));

    for (int frame = 0; frame < kFrames; ++frame)
    {
        // Frame i: the time is 1 + i, the time between frames is (1 + i) / 100.
        renderer->submitFrame(
            {.timeSeconds = 1.0 + frame, .deltaSeconds = static_cast<float>(1.0 + frame) / 100.0f});
    }
    // The blocks are read while no frame writes them: the join orders the writes of the frames
    // that were rendered before the reads. The last frame may have been dropped by it.
    RendererTestAccess::join(*renderer);
    EXPECT_FALSE(renderer->failure());

    const Extent extent = RendererTestAccess::swapchainExtent(*renderer);
    ASSERT_NE(camera.view(), camera.projectionMatrix(extent));

    const auto blocks = RendererTestAccess::frameBlocks(*renderer);
    ASSERT_EQ(blocks.size(), 2u);
    EXPECT_NE(blocks[0], blocks[1]);
    EXPECT_NE(blocks[0]->heapIndex(), blocks[1]->heapIndex());

    std::vector<float> times;
    for (const auto& buffer : blocks)
    {
        FrameBlock block{};
        {
            auto mapping = buffer->mapForWrite();
            std::ranges::copy(mapping.bytes().first(sizeof(block)),
                              std::as_writable_bytes(std::span{&block, 1u}).begin());
        }

        EXPECT_EQ(block.view, camera.view());
        EXPECT_EQ(block.projection, camera.projectionMatrix(extent));
        EXPECT_EQ(block.viewProjection, camera.viewProjection(extent));
        EXPECT_EQ(block.cameraPosition, Math::Vector4f(camera.position, 1.0f));
        EXPECT_EQ(
            block.viewport,
            Math::Vector2f(static_cast<float>(extent.width), static_cast<float>(extent.height)));

        // The time of one of the frames, with the time between frames of the same frame.
        const float frame = block.timeSeconds - 1.0f;
        EXPECT_GE(frame, 0.0f);
        EXPECT_LT(frame, static_cast<float>(kFrames));
        EXPECT_EQ(frame, std::floor(frame));
        EXPECT_EQ(block.deltaSeconds, block.timeSeconds / 100.0f);
        times.push_back(block.timeSeconds);
    }
    EXPECT_NE(times[0], times[1]);
}

/**
 * The commands that recordDraws() records retain the Frame block that the draws read by its heap
 * index: the block outlives the caller's last reference, which a frame drops when the next one
 * starts, and dies when the pool of its command buffer is reset.
 */
TEST_F(RendererTest, recordedDrawsRetainTheFrameBlockUntilThePoolIsReset)
{
    using namespace Graphics::Vulkan;

    auto                  renderer  = makeRenderer();
    const DevicePtr       device    = RendererTestAccess::device(*renderer);
    const auto            heap      = RendererTestAccess::heap(*renderer);
    const auto            queue     = device->getGraphicsQueue();
    const auto            fencePool = FencePool::create(device);
    const auto            pool      = device->createCommandBufferPool();
    const vk::Extent2D    extent{.width = 8u, .height = 8u};
    const RenderTargetPtr target = device->createRenderTarget(
        extent, gReadbackFormat, vk::ImageUsageFlagBits::eColorAttachment);

    HeapBufferPtr                   frameBlock = device->createHeapBuffer(heap, sizeof(FrameBlock));
    const std::weak_ptr<HeapBuffer> weak       = frameBlock;
    const FramePacket               packet;

    auto recorder = pool->record();
    recorder.bindDescriptorHeap(heap);
    recorder.setImageBarrier(target,
                             {.srcStageMask  = vk::PipelineStageFlagBits2::eTopOfPipe,
                              .dstStageMask  = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                              .dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite,
                              .oldLayout     = vk::ImageLayout::eUndefined,
                              .newLayout     = vk::ImageLayout::eColorAttachmentOptimal,
                              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                              .subresourceRange    = {.aspectMask = vk::ImageAspectFlagBits::eColor,
                                                      .levelCount = 1u,
                                                      .layerCount = 1u}});
    {
        const PipelineTable pipelines{device};
        auto                pass = recorder.beginRendering({{.attachment = target}});
        DrawRecording::recordDraws(pass, packet, {}, pipelines, frameBlock, extent);
    }
    frameBlock.reset();
    auto executable = recorder.finish();
    EXPECT_FALSE(weak.expired());

    queue->submit(*fencePool, {.commandBuffers = std::span{&executable, 1u}});
    EXPECT_FALSE(weak.expired());

    pool->reset();
    EXPECT_TRUE(weak.expired());
}

/**
 * The Frame block that the renderer writes reaches the shader: the shader paints the time, the time
 * between frames and the width of the viewport as the red, green and blue of every pixel. The time
 * of the packet is the one the pixels show, so a later packet shows a later time.
 */
TEST_F(RendererTest, frameBlockTimeAndViewportReachTheShader)
{
    constexpr std::uint32_t kSize = 32u;

    auto           renderer = makeRenderer();
    const Mesh     quad     = makeQuad(*renderer, 64.0f);
    const Material material = makeMaterial(*renderer, "FrameTime");

    const auto draw = [&](double timeSeconds, float deltaSeconds)
    {
        FramePacket packet{.camera = cameraBeforeThePlane(),
                           .frame  = {.timeSeconds = timeSeconds, .deltaSeconds = deltaSeconds}};
        RenderQueueTestAccess::addDraw(packet, quad, material, Math::Matrix4x4f{1.0f});

        return drawToPixels(*renderer, packet, kSize);
    };

    // 0.2 of 255 is 51, 0.4 is 102 and 0.6 is 153; a viewport of 32 over 32 is 1, which is 255.
    const std::vector<std::byte> first  = draw(0.2, 0.4f);
    const std::vector<std::byte> second = draw(0.6, 0.0f);

    for (std::size_t pixel = 0u; pixel < std::size_t{kSize} * kSize; ++pixel)
    {
        ASSERT_TRUE(pixelIs(first, pixel, 51u, 102u, gFull)) << "pixel " << pixel;
        ASSERT_TRUE(pixelIs(second, pixel, 153u, 0u, gFull)) << "pixel " << pixel;
    }
}

namespace
{
constexpr std::uint32_t gReadbackSize = 64u;

constexpr Math::Vector4f gRed{1.0f, 0.0f, 0.0f, 1.0f};
constexpr Math::Vector4f gGreen{0.0f, 1.0f, 0.0f, 1.0f};
constexpr Math::Vector4f gBlue{0.0f, 0.0f, 1.0f, 1.0f};

constexpr std::array<int, 4> gRedPixel{255, 0, 0, 255};
constexpr std::array<int, 4> gGreenPixel{0, 255, 0, 255};
constexpr std::array<int, 4> gBluePixel{0, 0, 255, 255};
} // namespace

/**
 * With the depth test on, the nearer of two overlapping quads wins whatever the order they are
 * drawn in, under a perspective camera. The near quad is the older mesh, so the sort puts it first
 * and the far quad, which a missing depth test would let overwrite it, second. The camera also
 * clears the target to its clear color, which shows where no quad is.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, nearerQuadWinsWithDepthUnderAPerspectiveCamera)
{
    auto renderer = makeRenderer();

    const Mesh nearQuad = makeColoredQuad(*renderer, {.half = 1.0f, .depth = 5.0f, .color = gRed});
    const Mesh farQuad =
        makeColoredQuad(*renderer, {.half = 4.0f, .depth = 10.0f, .color = gGreen});
    const Material material = makeMaterial(*renderer, "WorldSpace");

    // A right angle of view: the near quad fills a fifth of the half height, the far one two
    // fifths.
    FramePacket packet{.camera = {.projection = Perspective{.verticalFovRadians =
                                                                std::numbers::pi_v<float> / 2.0f},
                                  .clearColor = gBlue}};
    RenderQueueTestAccess::addDraw(packet, nearQuad, material, Math::Matrix4x4f{1.0f});
    RenderQueueTestAccess::addDraw(packet, farQuad, material, Math::Matrix4x4f{1.0f});

    const std::vector<std::byte> pixels = drawToPixels(*renderer, packet, gReadbackSize);

    constexpr std::uint32_t kMiddle = gReadbackSize / 2u;
    EXPECT_EQ(pixelAt(pixels, gReadbackSize, kMiddle, kMiddle), gRedPixel);
    EXPECT_EQ(pixelAt(pixels, gReadbackSize, kMiddle + 10u, kMiddle), gGreenPixel);
    EXPECT_EQ(pixelAt(pixels, gReadbackSize, 2u, 2u), gBluePixel);
}

/**
 * Alpha blending mixes the color of a draw with what is behind it by its alpha: half red over blue
 * is half of each; additive adds half the red to the blue; opaque ignores the alpha. The quad
 * does not write depth, which keeps its order free.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, blendModesMixTheColorWithWhatIsBehind)
{
    auto renderer = makeRenderer();

    const Mesh quad = makeColoredQuad(
        *renderer, {.half = 2.0f, .depth = 0.0f, .color = {1.0f, 0.0f, 0.0f, 0.5f}});

    const auto draw = [&](BlendMode blend)
    {
        const Material material =
            renderer->createMaterial({.shader = renderer->getShader("WorldSpace"),
                                      .state  = {.blend = blend, .depth = DepthMode::eTestOnly}});
        FramePacket packet{.camera = {.position = {0.0f, 0.0f, -20.0f}, .clearColor = gBlue}};
        RenderQueueTestAccess::addDraw(packet, quad, material, Math::Matrix4x4f{1.0f});

        const auto pixels = drawToPixels(*renderer, packet, gReadbackSize);

        return pixelAt(pixels, gReadbackSize, gReadbackSize / 2u, gReadbackSize / 2u);
    };

    // 0.5 of 255 is 127.5: either neighbour.
    EXPECT_TRUE(nearlyEqual(draw(BlendMode::eAlpha), {128, 0, 128, 255}));
    EXPECT_TRUE(nearlyEqual(draw(BlendMode::eAdditive), {128, 0, 255, 255}));
    EXPECT_EQ(draw(BlendMode::eOpaque), (std::array<int, 4>{255, 0, 0, 128}));
}

/**
 * World +Y is up the screen and the uv row 0 of a texture is at the top of the quad of the example:
 * a texture with a red row 0 and a blue row 1 is red in the upper half of the image and blue in the
 * lower, in the readback, whose rows run from the top.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, plusYIsUpAndTheFirstTextureRowIsAtTheTop)
{
    constexpr float kHalf = 2.0f;

    auto renderer = makeRenderer();

    // Two pixels wide, two high: row 0 red, row 1 blue.
    const std::array<std::uint8_t, 16> pixelsOfTexture{
        255, 0, 0, 255, 255, 0, 0, 255, 0, 0, 255, 255, 0, 0, 255, 255};
    const Texture texture =
        renderer->createTexture({.pixels = std::as_bytes(std::span{pixelsOfTexture}),
                                 .extent = {.width = 2u, .height = 2u},
                                 .format = TextureFormat::eRGBA8Unorm,
                                 .filter = Filter::eNearest});

    FramePacket packet{.camera = cameraBeforeThePlane(2.0f * kHalf)};
    RenderQueueTestAccess::addDraw(packet,
                                   makeQuad(*renderer, kHalf),
                                   makeTexturedMaterial(*renderer, texture),
                                   Math::Matrix4x4f{1.0f});

    const std::vector<std::byte> pixels = drawToPixels(*renderer, packet, gReadbackSize);

    EXPECT_EQ(pixelAt(pixels, gReadbackSize, 32u, 4u), gRedPixel);
    EXPECT_EQ(pixelAt(pixels, gReadbackSize, 32u, 59u), gBluePixel);
}

/**
 * eBack keeps the quad of the example, which is clockwise for a viewer, and discards the same quad
 * with its indices in the other order. The front face of the pipeline is clockwise: this is the
 * readback that shows what a driver does with that choice. A material without culling draws both.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, backFaceCullingKeepsTheClockwiseQuadAndDiscardsTheOtherWay)
{
    constexpr float kHalf = 2.0f;

    auto renderer = makeRenderer();

    const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    const Texture texture = renderer->createTexture({.pixels = std::as_bytes(std::span{white}),
                                                     .extent = {.width = 1u, .height = 1u},
                                                     .format = TextureFormat::eRGBA8Unorm});
    const auto    center  = [&](bool reversed, CullMode cull)
    {
        const Material material = renderer->createMaterial(
            {.shader     = renderer->getShader("Textured"),
             .parameters = {{"tint", Math::Vector4f{1.0f}}, {"albedo", texture}},
             .state      = {.cull = cull}});
        FramePacket packet{.camera = cameraBeforeThePlane(2.0f * kHalf)};
        RenderQueueTestAccess::addDraw(
            packet, makeQuad(*renderer, kHalf, reversed), material, Math::Matrix4x4f{1.0f});

        const auto pixels = drawToPixels(*renderer, packet, gReadbackSize);

        return pixelAt(pixels, gReadbackSize, gReadbackSize / 2u, gReadbackSize / 2u);
    };

    constexpr std::array<int, 4> kWhitePixel{255, 255, 255, 255};
    constexpr std::array<int, 4> kClearPixel{0, 0, 0, 255};
    EXPECT_EQ(center(false, CullMode::eBack), kWhitePixel);
    EXPECT_EQ(center(true, CullMode::eBack), kClearPixel);
    EXPECT_EQ(center(false, CullMode::eNone), kWhitePixel);
    EXPECT_EQ(center(true, CullMode::eNone), kWhitePixel);
}

/**
 * An object at the default Transform under the default Camera is on the screen: the default
 * orthographic projection has its near plane behind the camera, so the plane z = 0 is inside it.
 * A near plane of 1 would clip the quad away, silently.
 */
TEST_F(RendererTest, anObjectAtTheDefaultTransformUnderTheDefaultCameraIsVisible)
{
    auto renderer = makeRenderer();

    Scene scene;
    scene.createGameObject().addComponent<Camera>();
    scene.createGameObject().addComponent<MeshRenderer>(MeshRenderer{
        .mesh     = makeColoredQuad(*renderer, {.half = 1.0f, .depth = 0.0f, .color = gGreen}),
        .material = makeMaterial(*renderer, "WorldSpace")});
    scene.update({});

    Detail::DrawCheck check;
    RenderQueue       queue = RenderQueueTestAccess::make(check);
    scene.onDraw({}, queue);

    const std::vector<std::byte> pixels =
        drawToPixels(*renderer, RenderQueueTestAccess::packet(std::move(queue)), gReadbackSize);

    EXPECT_EQ(pixelAt(pixels, gReadbackSize, gReadbackSize / 2u, gReadbackSize / 2u), gGreenPixel);
}

/**
 * A Camera component with a perspective projection shows a distant object small: a quad of half 2
 * at the distance of 20, under a right angle of view, fills a tenth of the half height, so the
 * pixels around the middle of the image are the clear color of the camera, where the default
 * orthographic projection (10 units high) would have drawn the quad over them.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, aPerspectiveCameraComponentShowsADistantObjectSmall)
{
    auto renderer = makeRenderer();

    const auto pixelsOf = [&](const Camera& camera)
    {
        Scene scene;
        scene.createGameObject("camera").addComponent<Camera>(camera);
        GameObject quad                = scene.createGameObject("quad");
        quad.getTransform().position.z = 20.0f;
        quad.addComponent<MeshRenderer>(MeshRenderer{
            .mesh     = makeColoredQuad(*renderer, {.half = 2.0f, .depth = 0.0f, .color = gGreen}),
            .material = makeMaterial(*renderer, "WorldSpace")});
        scene.update({});

        Detail::DrawCheck check;
        RenderQueue       queue = RenderQueueTestAccess::make(check);
        scene.onDraw({}, queue);

        return drawToPixels(
            *renderer, RenderQueueTestAccess::packet(std::move(queue)), gReadbackSize);
    };

    constexpr std::uint32_t kMiddle     = gReadbackSize / 2u;
    const auto              perspective = pixelsOf(
        Camera{.projection = Perspective{.verticalFovRadians = std::numbers::pi_v<float> / 2.0f},
               .clearColor = gBlue});
    EXPECT_EQ(pixelAt(perspective, gReadbackSize, kMiddle, kMiddle), gGreenPixel);
    EXPECT_EQ(pixelAt(perspective, gReadbackSize, kMiddle + 10u, kMiddle), gBluePixel);

    const auto orthographic = pixelsOf(Camera{.clearColor = gBlue});
    EXPECT_EQ(pixelAt(orthographic, gReadbackSize, kMiddle + 10u, kMiddle), gGreenPixel);
}

/**
 * The frame lists the opaque draws first, sorted by pipeline, then material, then mesh, whatever
 * order they were submitted in; and the blended ones after them, from the farthest to the nearest
 * of the camera, draws at the same distance in the order they were submitted in. The expected
 * order is spelled out from the ids of the meshes and the materials, which grow in the order of
 * creation, and the pipelines, which are numbered in the order the opaque draws ask for them.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, drawsAreListedOpaqueFirstSortedAndBlendedFarToNear)
{
    auto renderer = makeRenderer();

    const Mesh first  = makeMesh(*renderer, gCompatibleLayout);
    const Mesh second = makeMesh(*renderer, gCompatibleLayout);
    const Mesh third  = makeMesh(*renderer, gCompatibleLayout);

    const Material program = makeMaterial(*renderer, "WorldSpace");
    const Material other   = makeMaterial(*renderer, "ScreenSpacePositionColor");
    // The program of the first, made after it: the same pipeline, the next material id.
    const Material later   = makeMaterial(*renderer, "WorldSpace");
    const Material blended = renderer->createMaterial(
        {.shader = renderer->getShader("WorldSpace"), .state = {.blend = BlendMode::eAlpha}});

    const auto atDepth = [](float depth)
    {
        return glm::translate(Math::Matrix4x4f{1.0f}, Math::Vector3f{0.0f, 0.0f, depth});
    };
    FramePacket packet;
    // 0..4 opaque: the pipelines are numbered 0 for "other" and 1 for "program", in this order.
    RenderQueueTestAccess::addDraw(packet, third, other, atDepth(0.0f));
    RenderQueueTestAccess::addDraw(packet, first, program, atDepth(0.0f));
    RenderQueueTestAccess::addDraw(packet, second, program, atDepth(0.0f));
    RenderQueueTestAccess::addDraw(packet, first, other, atDepth(0.0f));
    RenderQueueTestAccess::addDraw(packet, first, later, atDepth(0.0f));
    // 5..8 blended: z 5, 9, 5 and -3.
    RenderQueueTestAccess::addDraw(packet, first, blended, atDepth(5.0f));
    RenderQueueTestAccess::addDraw(packet, first, blended, atDepth(9.0f));
    RenderQueueTestAccess::addDraw(packet, second, blended, atDepth(5.0f));
    RenderQueueTestAccess::addDraw(packet, first, blended, atDepth(-3.0f));

    PipelineTable             pipelines{RendererTestAccess::device(*renderer)};
    std::vector<ResolvedDraw> resolved;
    DrawRecording::resolve(
        packet, pipelines, gReadbackFormat, RendererTestAccess::depthFormat(*renderer), resolved);

    std::vector<std::uint32_t> order;
    order.reserve(resolved.size());
    for (const ResolvedDraw& draw : resolved)
    {
        order.push_back(draw.index);
    }
    // -0 and +0 are one distance; a nearer draw has the larger key.
    EXPECT_EQ(DrawRecording::blendedKey(0.0f), DrawRecording::blendedKey(-0.0f));
    EXPECT_LT(DrawRecording::blendedKey(9.0f), DrawRecording::blendedKey(5.0f));
    EXPECT_LT(DrawRecording::blendedKey(0.0f), DrawRecording::blendedKey(-3.0f));
    EXPECT_LT(DrawRecording::blendedKey(5.0f), DrawRecording::blendedKey(0.0f));

    // Pipeline 0, "other": draws 3 (mesh first) and 0 (mesh third); pipeline 1, "program": by
    // material first, so draws 1 and 2 (the material "program") before draw 4 (the material
    // "later"), though draw 4 has the first mesh. Then z 9 (draw 6), z 5 in the order submitted
    // (5, then 7), and z -3 (draw 8).
    EXPECT_EQ(order, (std::vector<std::uint32_t>{3u, 0u, 1u, 2u, 4u, 6u, 5u, 7u, 8u}));
}

/**
 * Two blended quads that overlap mix with each other and with what is behind them, when the
 * material does not write depth (DepthMode::eTestOnly): red and green, half transparent, over
 * blue. The z is that of the world matrix of a draw. At the same z the one submitted last is on
 * top: red then green is (0.25, 0.5, 0.25), green then red is (0.5, 0.25, 0.25). At different z the
 * sort puts the farthest first, whatever the order of the submits: the nearer red over the farther
 * green is (0.5, 0.25, 0.25), though the red was submitted first. The default depth mode, which
 * writes, lets the first of two quads at the same z hide the second: only the red mixes, (0.5, 0,
 * 0.5).
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, blendedQuadsMixWhenTheyDoNotWriteDepthAndAreSortedFarToNear)
{
    auto renderer = makeRenderer();

    const auto pixelOf = [&](DepthMode             depth,
                             const Math::Vector4f& firstColor,
                             float                 firstDepth,
                             const Math::Vector4f& secondColor,
                             float                 secondDepth)
    {
        const Material blended =
            renderer->createMaterial({.shader = renderer->getShader("WorldSpace"),
                                      .state  = {.blend = BlendMode::eAlpha, .depth = depth}});
        FramePacket packet{.camera = {.position = {0.0f, 0.0f, -20.0f}, .clearColor = gBlue}};
        RenderQueueTestAccess::addDraw(
            packet,
            makeColoredQuad(*renderer, {.half = 2.0f, .depth = 0.0f, .color = firstColor}),
            blended,
            glm::translate(Math::Matrix4x4f{1.0f}, Math::Vector3f{0.0f, 0.0f, firstDepth}));
        RenderQueueTestAccess::addDraw(
            packet,
            makeColoredQuad(*renderer, {.half = 2.0f, .depth = 0.0f, .color = secondColor}),
            blended,
            glm::translate(Math::Matrix4x4f{1.0f}, Math::Vector3f{0.0f, 0.0f, secondDepth}));

        const auto pixels = drawToPixels(*renderer, packet, gReadbackSize);

        return pixelAt(pixels, gReadbackSize, gReadbackSize / 2u, gReadbackSize / 2u);
    };

    constexpr Math::Vector4f kHalfRed{1.0f, 0.0f, 0.0f, 0.5f};
    constexpr Math::Vector4f kHalfGreen{0.0f, 1.0f, 0.0f, 0.5f};

    EXPECT_TRUE(nearlyEqual(pixelOf(DepthMode::eTestOnly, kHalfRed, 0.0f, kHalfGreen, 0.0f),
                            {64, 128, 64, 255}));
    EXPECT_TRUE(nearlyEqual(pixelOf(DepthMode::eTestOnly, kHalfGreen, 0.0f, kHalfRed, 0.0f),
                            {128, 64, 64, 255}));
    EXPECT_TRUE(nearlyEqual(pixelOf(DepthMode::eTestOnly, kHalfRed, -5.0f, kHalfGreen, 5.0f),
                            {128, 64, 64, 255}));
    EXPECT_TRUE(nearlyEqual(pixelOf(DepthMode::eTestAndWrite, kHalfRed, 0.0f, kHalfGreen, 0.0f),
                            {128, 0, 128, 255}));
}

/**
 * DepthMode::eTestOnly tests against the depth buffer and does not write it. A nearer quad of that
 * mode, drawn first, leaves no depth behind, so a farther opaque quad drawn after it on the same
 * pixels shows; a nearer quad of the default mode hides the farther one. The nearer quad is drawn
 * first because its pipeline is asked for first, and the sort keeps the pipelines in that order.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, aTestOnlyQuadLeavesNoDepthForTheQuadBehindIt)
{
    auto renderer = makeRenderer();

    const Mesh nearQuad = makeColoredQuad(*renderer, {.half = 2.0f, .depth = -5.0f, .color = gRed});
    const Mesh farQuad = makeColoredQuad(*renderer, {.half = 2.0f, .depth = 5.0f, .color = gGreen});

    const auto centerWith = [&](DepthMode nearDepth)
    {
        const Material nearMaterial = renderer->createMaterial(
            {.shader = renderer->getShader("WorldSpace"), .state = {.depth = nearDepth}});
        const Material farMaterial = makeMaterial(*renderer, "WorldSpace");
        // Drawn in the order of their pipelines: the state of the near one comes first.
        FramePacket packet{.camera = {.position = {0.0f, 0.0f, -20.0f}, .clearColor = gBlue}};
        RenderQueueTestAccess::addDraw(packet, nearQuad, nearMaterial, Math::Matrix4x4f{1.0f});
        RenderQueueTestAccess::addDraw(packet, farQuad, farMaterial, Math::Matrix4x4f{1.0f});

        const auto pixels = drawToPixels(*renderer, packet, gReadbackSize);

        return pixelAt(pixels, gReadbackSize, gReadbackSize / 2u, gReadbackSize / 2u);
    };

    EXPECT_EQ(centerWith(DepthMode::eTestOnly), gGreenPixel);
    EXPECT_EQ(centerWith(DepthMode::eTestAndWrite), gRedPixel);
}

/**
 * A real frame clears its color target to the clear color of the camera of the frame, and renders
 * into a depth target that it clears to 1: no swapchain image can be read back, so the call that
 * the render thread makes to begin the pass is read instead.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, aRealFrameClearsToTheCameraColorAndRendersIntoAClearedDepthTarget)
{
    constexpr Math::Vector4f kClear{0.25f, 0.5f, 0.75f, 1.0f};

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [kClear](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.setCamera(CameraView{.clearColor = kClear});
        }));

    const RenderingSpy spy{RendererTestAccess::dispatcher(*renderer)};
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }
    RendererTestAccess::join(*renderer);
    ASSERT_FALSE(renderer->failure());

    const std::vector<SpiedRendering> renderings = spy.renderings();
    ASSERT_GE(renderings.size(), 2u);
    for (const SpiedRendering& rendering : renderings)
    {
        EXPECT_EQ(rendering.colorCount, 1u);
        EXPECT_EQ(rendering.colorLoadOp, vk::AttachmentLoadOp::eClear);
        EXPECT_EQ(rendering.colorClear, (std::array<float, 4>{0.25f, 0.5f, 0.75f, 1.0f}));
        EXPECT_TRUE(rendering.hasDepth);
        EXPECT_EQ(rendering.depthLoadOp, vk::AttachmentLoadOp::eClear);
        EXPECT_EQ(rendering.depthClear, 1.0f);
    }
}

/**
 * Frames in flight draw into one depth target, and the barrier at the start of a frame makes the
 * depth writes of the frame before visible to it. The synchronization validation of the layer,
 * which the fixture turns on when the layer is installed, would report a depth write that races
 * the write of the frame before; every validation message fails the test. The test asserts that the
 * layer is installed: without it nothing here would see anything.
 */
TEST_F(RendererTest, framesInFlightShareTheDepthTargetWithoutASynchronizationMessage)
{
    ASSERT_TRUE(mValidationInstalled)
        << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils is missing, so this test would see "
           "nothing.";

    Renderer::Config config = makeConfig();
    config.framesInFlight   = 2u;

    auto renderer = makeRenderer(config);
    renderer->addRenderable(
        drawing(makeColoredQuad(*renderer, {.half = 4.0f, .depth = 0.0f, .color = gRed}),
                makeMaterial(*renderer, "WorldSpace")));

    constexpr int kFrames = 12;
    for (int frame = 0; frame < kFrames; ++frame)
    {
        renderer->submitFrame({});
    }
    RendererTestAccess::join(*renderer);

    EXPECT_FALSE(renderer->failure());
}

/**
 * The depth target has the size of the image that the frame acquired. A resize that shrinks the
 * swapchain below the target, and one that grows it above, draw without a validation message and
 * leave a target of the size of the swapchain: attachments have to cover the render area.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, resizeGivesTheDepthTargetTheSizeOfTheSwapchain)
{
    ASSERT_TRUE(mValidationInstalled);

    auto renderer = makeRenderer();
    renderer->addRenderable(
        drawing(makeColoredQuad(*renderer, {.half = 4.0f, .depth = 0.0f, .color = gRed}),
                makeMaterial(*renderer, "WorldSpace")));

    for (const std::uint32_t size : {gStartExtent.width, gResizedExtent, gResizedAgainExtent * 2u})
    {
        renderer->resize({.width = size, .height = size});
        for (int frame = 0; frame < 3; ++frame)
        {
            renderer->submitFrame({});
        }
        EXPECT_TRUE(becomesTrue(
            [&]
            {
                return RendererTestAccess::packets(*renderer).empty();
            }));
    }
    RendererTestAccess::join(*renderer);

    EXPECT_FALSE(renderer->failure());
    EXPECT_EQ(RendererTestAccess::depthExtent(*renderer),
              RendererTestAccess::swapchainExtent(*renderer));
}

// NOLINTEND(readability-magic-numbers)
} // namespace VOG::Tests
