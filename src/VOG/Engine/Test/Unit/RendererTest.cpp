#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/DrawRecording.hpp"
#include "VOG/Engine/Renderer/FrameBlocks.hpp"
#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineCache.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>
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
#include <optional>
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
    /** While no render thread runs. */
    static Extent
    swapchainExtent(const Renderer& renderer)
    {
        const vk::Extent2D extent = renderer.mSwapchain->getExtent();

        return {.width = extent.width, .height = extent.height};
    }

    /** While no render thread runs. */
    static std::size_t
    pipelineCount(const Renderer& renderer)
    {
        return renderer.mPipelineCache->mEntries.size();
    }

    static FramePacketQueue&
    packets(Renderer& renderer)
    {
        return *renderer.mPackets;
    }

    /** Uploads that createMesh() and createTexture() queued and no submitted frame has recorded
     *  yet. */
    static std::size_t
    pendingUploadCount(Renderer& renderer)
    {
        const std::scoped_lock lock{renderer.mUploadMutex};

        return renderer.mPendingUploads.size();
    }

    /** The staging buffer of pending upload @p index; it must exist. */
    static std::weak_ptr<Graphics::Vulkan::Buffer>
    pendingStaging(Renderer& renderer, std::size_t index)
    {
        const std::scoped_lock lock{renderer.mUploadMutex};

        return renderer.mPendingUploads.at(index).staging;
    }

    static Graphics::Vulkan::DevicePtr
    device(const Renderer& renderer)
    {
        return renderer.mVulkanDevice;
    }

    static std::shared_ptr<Graphics::Vulkan::DescriptorHeap>
    heap(const Renderer& renderer)
    {
        return renderer.mHeap;
    }

    /** While no render thread runs: the pipeline of each draw of @p packet for a color target of
     *  @p format. */
    static std::vector<Graphics::Vulkan::GraphicsPipelinePtr>
    pipelines(Renderer& renderer, const FramePacket& packet, vk::Format format)
    {
        std::vector<Graphics::Vulkan::GraphicsPipelinePtr> pipelines;
        for (const auto& draw : RenderQueueTestAccess::draws(packet.draws))
        {
            pipelines.push_back(renderer.mPipelineCache->get(
                HandleTestAccess::data(draw.mesh), HandleTestAccess::data(draw.material), format));
        }

        return pipelines;
    }

    /** While no render thread runs: records the copies of the pending uploads into @p recorder,
     *  for the queue family @p family of the pool, which also draws. */
    static void
    recordPendingUploads(Renderer&                                renderer,
                         Graphics::Vulkan::CommandBufferRecorder& recorder,
                         std::uint32_t                            family)
    {
        for (const Renderer::PendingUpload& upload : renderer.mPendingUploads)
        {
            if (const auto* buffer = std::get_if<Graphics::Vulkan::BufferPtr>(&upload.destination))
            {
                recorder.copyBuffer(upload.staging, *buffer, family);
            }
            else
            {
                recorder.copyBufferToTexture(
                    upload.staging,
                    std::get<Graphics::Vulkan::TexturePtr>(upload.destination),
                    family);
            }
        }
    }

    static const vk::PhysicalDeviceLimits&
    limits(const Renderer& renderer)
    {
        return renderer.mVulkanDevice->limits;
    }

    /** While no render thread runs: the buffers of the Frame block that the frames wrote, one per
     *  frame slot that was rendered into. */
    static std::vector<Graphics::Vulkan::HeapBufferPtr>
    frameBlocks(const Renderer& renderer)
    {
        std::vector<Graphics::Vulkan::HeapBufferPtr> buffers;
        for (const auto& [slot, buffer] : renderer.mFrameBlocks->mBlocks)
        {
            buffers.push_back(buffer);
        }

        return buffers;
    }

    /** The function table that every command buffer of the renderer's device records through. */
    static vk::raii::detail::DeviceDispatcher&
    dispatcher(Renderer& renderer)
    {
        // The device owns the table and keeps it mutable; getDispatcher() only hands it out const.
        return const_cast<vk::raii::detail::DeviceDispatcher&>(
            *renderer.mVulkanDevice->getDispatcher());
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
            .appName          = "VOG Renderer Test",
            .engineName       = "Test",
            .extensions       = {"VK_EXT_headless_surface"},
            .framesInFlight   = 2u,
            .shaderSourcePath = shaderPath,
            .extent           = gStartExtent,
        };
        if (mValidationInstalled)
        {
            config.layers              = {"VK_LAYER_KHRONOS_validation"};
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

/**
 * The sink sees the construction: the loader reports an extension it does not know, while
 * vkCreateInstance runs, which is before the Instance's persistent messenger exists. Only the
 * messenger that the sink puts on the pNext chain of the instance can catch it.
 */
TEST_F(RendererTest, errorDuringConstructionIsRecorded)
{
    ASSERT_TRUE(mValidationInstalled)
        << "VK_LAYER_KHRONOS_validation or VK_EXT_debug_utils is missing, so no RendererTest can "
           "see a validation error.";
    Renderer::Config config = makeConfig();
    config.extensions.emplace_back("VK_FAKE_extension");

    EXPECT_THROW(std::ignore = makeRenderer(config), vk::ExtensionNotPresentError);

    EXPECT_TRUE(takeError("VK_FAKE_extension"));
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

constexpr std::size_t gQuadStride = 24u;

/** As the WorldSpace shader reads them: a vec3 position (fed two floats) and a vec4 color. */
std::vector<VertexAttribute>
compatibleLayout()
{
    return {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat4, sizeOf(VertexFormat::eFloat2)}};
}

/** WorldSpace also reads location 1, which this layout lacks. */
std::vector<VertexAttribute>
incompatibleLayout()
{
    return {{VertexFormat::eFloat2, 0u}};
}

constexpr std::size_t gTexturedStride = 16u;
constexpr std::size_t gPixelSize      = 4u;

/** As the Textured shader reads them: a position and a uv, two floats each. */
std::vector<VertexAttribute>
texturedLayout()
{
    return {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat2, sizeOf(VertexFormat::eFloat2)}};
}

Mesh
makeTexturedMesh(Renderer& renderer)
{
    return renderer.createMesh({.vertices   = std::vector<std::byte>(3u * gTexturedStride),
                                .stride     = gTexturedStride,
                                .attributes = texturedLayout()});
}

/** A black 2x2 texture. */
Texture
makeTexture(Renderer& renderer)
{
    const std::vector<std::byte> pixels(std::size_t{2u} * 2u * gPixelSize);

    return renderer.createTexture({.pixels = pixels, .width = 2u, .height = 2u});
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

Mesh
makeMesh(Renderer& renderer, const std::vector<VertexAttribute>& attributes)
{
    return renderer.createMesh({.vertices   = std::vector<std::byte>(3u * gQuadStride),
                                .stride     = gQuadStride,
                                .attributes = attributes});
}

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
    renderer->start();
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
    renderer->start();

    EXPECT_THROW(renderer->submitFrame({}), std::runtime_error);
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);

    EXPECT_NO_THROW(renderer->submitFrame({}));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A draw whose vertex layout the shader cannot read ends the render thread, and stop() hands the
 * exception to the caller. While it is down, submitFrame() does not wait for it: the dead thread
 * keeps taking and dropping packets. The renderable stays: after a restart it draws a mesh that
 * fits.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, renderThreadFailureIsReportedAndRestartable)
{
    auto renderer = makeRenderer();
    auto material = makeMaterial(*renderer, "WorldSpace");
    auto bad      = makeMesh(*renderer, incompatibleLayout());
    auto good     = makeMesh(*renderer, compatibleLayout());

    bool first = true;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit(first ? bad : good, material, Math::Matrix4x4f{1.0f});
            first = false;
        }));
    renderer->start();
    renderer->submitFrame({});

    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return renderer->getState() == Renderer::State::eFailed;
        }));

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
            // Wakes the submit that waits. The test has failed already, so losing the exception
            // to this stop() does not matter.
            std::ignore = renderer->stop();
        }));

    const std::exception_ptr failure = renderer->stop();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), std::invalid_argument);
    EXPECT_EQ(renderer->getState(), Renderer::State::eStopped);

    renderer->start();
    renderer->submitFrame({});
    renderer->submitFrame({});
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
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
    renderer->start();

    renderer->submitFrame({});
    EXPECT_EQ(log, (std::vector<std::string>{"parent.onDraw"}));

    renderer->submitFrame({});
    EXPECT_EQ(log, (std::vector<std::string>{"parent.onDraw", "child.onDraw"}));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A mesh whose attributes do not cover the shader's inputs is a bug of the renderable: it fails
 * the render thread, when the pipeline is looked up, instead of being drawn with garbage.
 */
TEST_F(RendererTest, incompatibleMeshFailsRenderThread)
{
    auto renderer = makeRenderer();
    renderer->addRenderable(
        drawing(makeMesh(*renderer, incompatibleLayout()), makeMaterial(*renderer, "WorldSpace")));
    renderer->start();
    renderer->submitFrame({});

    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return renderer->getState() == Renderer::State::eFailed;
        }));

    const std::exception_ptr failure = renderer->stop();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), std::invalid_argument);
}

/**
 * A draw without a mesh or a material is dropped when the packet is built, and the other draws
 * of the frame are drawn: the valid draw's pipeline is in the cache afterwards. A default handle is
 * empty, which is what "without" means, and a created one is not; a copy shares the mesh.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, drawWithoutMeshOrMaterialIsSkipped)
{
    auto renderer = makeRenderer();
    auto material = makeMaterial(*renderer, "WorldSpace");
    auto mesh     = makeMesh(*renderer, compatibleLayout());

    EXPECT_FALSE(Mesh{});
    EXPECT_FALSE(Material{});
    EXPECT_NE(makeMesh(*renderer, compatibleLayout()), mesh);
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
    renderer->start();
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());

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
    renderer->start();
    renderer->submitFrame({.timeSeconds = kTime});
    EXPECT_FALSE(renderer->stop());

    EXPECT_EQ(drawnOn, std::this_thread::get_id());
    EXPECT_EQ(timeSeen, kTime);
}

/**
 * submitFrame() on a renderer that is not running returns at once and calls no renderable. It would
 * otherwise wait for a consumer that does not exist.
 */
TEST_F(RendererTest, submitFrameWhenStoppedReturns)
{
    std::size_t draws = 0u;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&draws](const FrameContext& /*context*/, RenderQueue& /*queue*/)
        {
            ++draws;
        }));

    // The second call would wait for a consumer if the first had queued a frame. start() gives it
    // one.
    const auto submitTwice = [&]
    {
        return returnsInTime(
            [&]
            {
                renderer->submitFrame({});
                renderer->submitFrame({});
            },
            [&]
            {
                renderer->start();
            });
    };

    EXPECT_TRUE(submitTwice());
    renderer->start();
    EXPECT_FALSE(renderer->stop());
    EXPECT_TRUE(submitTwice());

    EXPECT_EQ(draws, 0u);
}

/**
 * trySubmitFrame() calls no renderable while the render thread still holds the previous frame, and
 * none when stopped. A zero-sized window makes the render thread sleep after each frame, which
 * keeps the slot full long enough to see.
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

    renderer->trySubmitFrame({});
    EXPECT_EQ(draws, 0u);

    renderer->resize({});
    renderer->start();
    std::size_t calls    = 0u;
    const auto  deadline = std::chrono::steady_clock::now() + gDeadline;
    while (calls == draws && std::chrono::steady_clock::now() < deadline)
    {
        renderer->trySubmitFrame({});
        ++calls;
    }
    EXPECT_LT(draws, calls) << "No call found the slot full.";
    EXPECT_FALSE(renderer->stop());

    const std::size_t drawsWhenStopped = draws;
    renderer->trySubmitFrame({});
    EXPECT_EQ(draws, drawsWhenStopped);
}

/** stop() wakes a render thread that waits for the next frame. */
TEST_F(RendererTest, stopInterruptsIdleRenderThread)
{
    auto renderer = makeRenderer();
    renderer->start();

    EXPECT_TRUE(returnsInTime(
        [&]
        {
            EXPECT_FALSE(renderer->stop());
        },
        [&]
        {
            // stop() holds the state lock while it waits, so submitFrame() would wait for it.
            RendererTestAccess::packets(*renderer).push({}, {});
        }));
}

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshRejectsMalformedDescription)
{
    constexpr std::size_t        kStride = 8u;
    const std::vector<std::byte> bytes(2u * kStride);
    const VertexAttribute        float2{VertexFormat::eFloat2, 0u};

    auto renderer = makeRenderer();

    EXPECT_THROW(std::ignore = renderer->createMesh({.stride = kStride, .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createMesh({.vertices = bytes, .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createMesh({.vertices   = std::span{bytes}.first(kStride + 1u),
                                           .stride     = kStride,
                                           .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(
        std::ignore = renderer->createMesh(
            {.vertices = bytes, .stride = kStride, .attributes = {{VertexFormat::eFloat2, 4u}}}),
        std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createMesh({.vertices   = bytes,
                                           .stride     = kStride,
                                           .attributes = std::vector(9u, VertexAttribute{})}),
                 std::invalid_argument);

    // A stride or an offset off a multiple of 4 puts a component at a misaligned address.
    constexpr std::size_t        kOddStride = 10u;
    const std::vector<std::byte> oddBytes(2u * kOddStride);
    EXPECT_THROW(std::ignore = renderer->createMesh({.vertices   = oddBytes,
                                                     .stride     = kOddStride,
                                                     .attributes = {{VertexFormat::eFloat2, 0u}}}),
                 std::invalid_argument);
    // The attribute fits the stride (2 + 8 <= 12), so only the multiple-of-4 rule rejects it.
    constexpr std::size_t        kFitStride = 12u;
    const std::vector<std::byte> fitBytes(2u * kFitStride);
    EXPECT_THROW(std::ignore = renderer->createMesh({.vertices   = fitBytes,
                                                     .stride     = kFitStride,
                                                     .attributes = {{VertexFormat::eFloat2, 2u}}}),
                 std::invalid_argument);

    // One vertex whose stride is the device maximum plus 4. Devices with a huge maximum would make
    // the vertex too big to test; lavapipe's is small.
    const std::uint32_t maxStride =
        RendererTestAccess::limits(*renderer).maxVertexInputBindingStride;
    constexpr std::uint32_t kLargestTestable = 1u << 16u;
    if (maxStride <= kLargestTestable)
    {
        const std::vector<std::byte> hugeVertex(std::size_t{maxStride} + 4u);
        EXPECT_THROW(std::ignore =
                         renderer->createMesh({.vertices   = hugeVertex,
                                               .stride     = maxStride + 4u,
                                               .attributes = {{VertexFormat::eFloat2, 0u}}}),
                     std::invalid_argument);
    }

    const Mesh mesh =
        renderer->createMesh({.vertices = bytes, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(HandleTestAccess::data(mesh).vertexCount, 2u);
}

/** Indices are Renderer's to check: without robustBufferAccess an index past the vertices reads
 * outside the buffer. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshRejectsMalformedIndices)
{
    constexpr std::size_t        kStride = 8u;
    const std::vector<std::byte> bytes(3u * kStride);
    const VertexAttribute        float2{VertexFormat::eFloat2, 0u};
    const std::uint32_t          triangle[] = {0u, 1u, 2u};
    const std::uint32_t          quad[]     = {0u, 1u, 2u, 0u};
    const std::uint32_t          pastEnd[]  = {0u, 1u, 3u};

    auto renderer = makeRenderer();

    EXPECT_THROW(
        std::ignore = renderer->createMesh(
            {.vertices = bytes, .indices = quad, .stride = kStride, .attributes = {float2}}),
        std::invalid_argument);
    EXPECT_THROW(
        std::ignore = renderer->createMesh(
            {.vertices = bytes, .indices = pastEnd, .stride = kStride, .attributes = {float2}}),
        std::invalid_argument);

    const Mesh indexed = renderer->createMesh(
        {.vertices = bytes, .indices = triangle, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(indexed);
    EXPECT_EQ(HandleTestAccess::data(indexed).indexCount, 3u);

    const Mesh plain =
        renderer->createMesh({.vertices = bytes, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(plain);
    EXPECT_EQ(HandleTestAccess::data(plain).indexCount, 0u);
}

/**
 * The indices follow the vertices, at a multiple of 4 bytes because the stride is one. The staging
 * buffer holds both.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMeshStagesIndicesAfterVertices)
{
    constexpr std::size_t kStride = 12u;
    // 36 bytes of vertices: the indices start at 36.
    std::vector<std::byte> vertices(3u * kStride);
    for (std::size_t i = 0u; i < vertices.size(); ++i)
    {
        vertices[i] = static_cast<std::byte>(i + 1u);
    }
    const std::uint32_t indices[] = {2u, 1u, 0u};

    auto renderer = makeRenderer();

    const Mesh mesh = renderer->createMesh({.vertices   = vertices,
                                            .indices    = indices,
                                            .stride     = kStride,
                                            .attributes = {{VertexFormat::eFloat2, 0u}}});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(HandleTestAccess::data(mesh).indexOffset, 36u);

    // The bytes wait in the staging buffer, which the render thread copies into the buffer of
    // the mesh. Reading it back through mapForWrite() is meant for writing; it is legal
    // on lavapipe, whose host memory is coherent.
    ASSERT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 1u);
    const auto staging = RendererTestAccess::pendingStaging(*renderer, 0u).lock();
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
}

/** An indexed mesh reaches the render thread; the validation layer checks the draw. */
TEST_F(RendererTest, indexedMeshIsDrawn)
{
    const std::uint32_t indices[] = {0u, 1u, 2u};

    auto renderer = makeRenderer();
    auto mesh     = renderer->createMesh({.vertices   = std::vector<std::byte>(3u * gQuadStride),
                                          .indices    = indices,
                                          .stride     = gQuadStride,
                                          .attributes = compatibleLayout()});
    renderer->addRenderable(drawing(mesh, makeMaterial(*renderer, "WorldSpace")));
    renderer->start();
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
    EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 1u);
}

/**
 * A draw does not bind what the draw before it bound in the same frame. One frame draws, in order:
 * mesh A (indexed) twice, A with another program, a non-indexed mesh B, A again, and an indexed
 * mesh C. Binds that reach the driver, with the reason for each:
 *   pipeline 2: the first draw, and A's second program;
 *   vertex buffers 4: A, B, A again (B came between), C;
 *   index buffers 2: A, and C. A is not bound again after B, because a non-indexed draw leaves the
 *   bound index buffer as it was.
 * Without the skip it is 6, 6 and 5.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, repeatedBindsAreSkipped)
{
    const std::uint32_t indices[] = {0u, 1u, 2u};
    const auto          vertices  = std::vector<std::byte>(3u * gQuadStride);

    auto renderer = makeRenderer();
    auto indexed  = [&]
    {
        return renderer->createMesh({.vertices   = vertices,
                                     .indices    = indices,
                                     .stride     = gQuadStride,
                                     .attributes = compatibleLayout()});
    };
    const Mesh     meshA = indexed();
    const Mesh     meshB = makeMesh(*renderer, compatibleLayout());
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

    {
        const BindCounter counter{RendererTestAccess::dispatcher(*renderer)};
        renderer->start();
        renderer->submitFrame({});
        renderer->submitFrame({});
        EXPECT_FALSE(renderer->stop());
        EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 2u);
        EXPECT_EQ(gPipelineBinds, 2);
        EXPECT_EQ(gVertexBufferBinds, 4);
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

    const std::vector<std::byte> pixels(std::size_t{2u} * 3u * gPixelSize);
    EXPECT_NO_THROW(std::ignore =
                        renderer->createTexture({.pixels = pixels, .width = 2u, .height = 3u}));
    EXPECT_NO_THROW(
        std::ignore = renderer->createTexture(
            {.pixels = pixels, .width = 2u, .height = 3u, .format = TextureFormat::eRGBA8Unorm}));

    EXPECT_THROW(std::ignore = renderer->createTexture({.width = 2u, .height = 3u}),
                 std::invalid_argument);
    // No pixels for no extent are still no texture.
    EXPECT_THROW(std::ignore = renderer->createTexture({}), std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createTexture({.pixels = pixels, .width = 0u, .height = 3u}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createTexture({.pixels = pixels, .width = 2u, .height = 0u}),
                 std::invalid_argument);
    // One past the device limit: the pixels fit the byte count, the image does not exist.
    const std::uint32_t tooWide = RendererTestAccess::limits(*renderer).maxImageDimension2D + 1u;
    const std::vector<std::byte> tooWidePixels(std::size_t{tooWide} * gPixelSize);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = tooWidePixels, .width = tooWide, .height = 1u}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore = renderer->createTexture(
                     {.pixels = tooWidePixels, .width = 1u, .height = tooWide}),
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
        EXPECT_THROW(std::ignore = renderer->createTexture(
                         {.pixels = huge.bytes(), .width = kHugeSide, .height = kHugeSide}),
                     std::invalid_argument);
    }
#endif
    // One byte too few, one byte too many, and the right size for the other extent.
    EXPECT_THROW(
        std::ignore = renderer->createTexture(
            {.pixels = std::span{pixels}.first(pixels.size() - 1u), .width = 2u, .height = 3u}),
        std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createTexture({.pixels = pixels, .width = 2u, .height = 2u}),
                 std::invalid_argument);
    EXPECT_THROW(std::ignore =
                     renderer->createTexture({.pixels = pixels, .width = 6u, .height = 6u}),
                 std::invalid_argument);

    // Only the two valid calls left an upload.
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 2u);
}

/**
 * A frame in flight keeps the mesh's buffer alive after the renderer let go of the mesh: the frame
 * that drew it is rendered and gone, but its slot is not reused yet. Once the slot is reused,
 * nothing holds the buffer. The upload went in frames before, and is released by then (the
 * staging buffer has expired), so only the draw's recording holds the buffer: the copy and the
 * acquire of the upload frame would otherwise hide a missing retention of the draw.
 *
 * A popped packet is rendered unless the acquire skips it; on a headless surface only an acquire
 * timeout does, and the first frames find a free image. Submitting N+1 returns only once N has been
 * popped. The test submits no third frame after the draw, and reusing the slot of the draw takes
 * one (the renderer has two frame slots).
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

    Mesh mesh        = makeMesh(*renderer, compatibleLayout());
    auto weakMesh    = HandleTestAccess::weak(mesh);
    auto weakBuffer  = std::weak_ptr<Graphics::Vulkan::Buffer>{HandleTestAccess::data(mesh).buffer};
    auto weakStaging = RendererTestAccess::pendingStaging(*renderer, 0u);

    // Upload first, and wait for the frame that recorded it to be gone: its staging buffer is
    // released with it, and so is every hold of the upload on the buffer.
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakStaging.expired();
        }));

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
    EXPECT_FALSE(renderer->stop());

    EXPECT_TRUE(weakMesh.expired());
    EXPECT_FALSE(weakBuffer.expired());

    // Frames that draw nothing cycle through the slots until the draw's slot is reused; the
    // deadline keeps a leak from hanging the test.
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakBuffer.expired();
        }));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A frame in flight keeps the image of a texture alive after the renderer let go of the material:
 * the draw recorded the texture, and the frame's slot is not reused yet. The upload went in a
 * frame before, and is released by then, so only the draw's recording holds the image.
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
    auto weakStaging = RendererTestAccess::pendingStaging(*renderer, 0u);

    // Upload first, and wait for the frame that recorded it to be gone: its staging buffer is
    // released with it.
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakStaging.expired();
        }));

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
    EXPECT_FALSE(renderer->stop());

    EXPECT_TRUE(weakTexture.expired());
    EXPECT_FALSE(weakImage.expired());

    // Frames that draw nothing cycle through the slots until the draw's slot is reused.
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakImage.expired();
        }));
    EXPECT_FALSE(renderer->stop());
}

/** As for the texture, for the parameter block of a material. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, parameterBlockOutlivesMaterialWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    std::weak_ptr<Graphics::Vulkan::HeapBuffer> weakBlock;
    {
        Material material = makeMaterial(*renderer, "WorldSpace");
        weakBlock         = HandleTestAccess::data(material).block;
        renderer->addRenderable(std::make_shared<LambdaRenderable>(
            [mesh = makeMesh(*renderer, compatibleLayout()), material = std::move(material)](
                const FrameContext& /*context*/, RenderQueue& queue) mutable
            {
                queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
                mesh     = {};
                material = {};
            }));
    }

    renderer->start();
    renderer->submitFrame({});
    renderer->submitFrame({});
    EXPECT_FALSE(renderer->stop());

    EXPECT_FALSE(weakBlock.expired());

    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return weakBlock.expired();
        }));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A frame that is dropped does not consume the uploads, of a texture and of a mesh: a zero-sized
 * window ends each frame before it records anything, and the uploads are still pending. Once the
 * window has a size again, the next frame records them and the list drains.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, uploadsSurviveDroppedFrame)
{
    auto renderer = makeRenderer();
    auto texture  = makeTexture(*renderer);
    auto mesh     = makeMesh(*renderer, compatibleLayout());

    renderer->resize({});
    renderer->start();
    // The second call returns only after the render thread took, and dropped, the first frame.
    renderer->submitFrame({});
    renderer->submitFrame({});
    EXPECT_FALSE(renderer->stop());
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 2u);

    renderer->resize(gStartExtent);
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return RendererTestAccess::pendingUploadCount(*renderer) == 0u;
        }));
    EXPECT_FALSE(renderer->stop());
}

/**
 * The staging buffer of a texture and of a mesh lives in the pending list until the frame that
 * recorded the upload has been submitted, then in that frame's transfer command buffer, and is gone
 * once the frame's slot is reused.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, uploadsAreRecordedAndStagingReleased)
{
    auto renderer       = makeRenderer();
    auto texture        = makeTexture(*renderer);
    auto mesh           = makeMesh(*renderer, compatibleLayout());
    auto textureStaging = RendererTestAccess::pendingStaging(*renderer, 0u);
    auto meshStaging    = RendererTestAccess::pendingStaging(*renderer, 1u);
    EXPECT_FALSE(textureStaging.expired());
    EXPECT_FALSE(meshStaging.expired());

    // One frame only: the slot of the upload is reused by the third frame at the earliest, so the
    // render thread cannot get there while this thread waits for the list to empty.
    renderer->start();
    renderer->submitFrame({});
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            return RendererTestAccess::pendingUploadCount(*renderer) == 0u;
        }));
    EXPECT_FALSE(textureStaging.expired());
    EXPECT_FALSE(meshStaging.expired());

    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return textureStaging.expired() && meshStaging.expired();
        }));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A render thread that fails in resolvePipelines, before it records anything, leaves the uploads
 * (a texture and two meshes) pending, and they are recorded after stop() and start().
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, pendingUploadsSurviveRenderThreadFailureAndRestart)
{
    auto renderer = makeRenderer();
    auto texture  = makeTexture(*renderer);
    auto material = makeMaterial(*renderer, "WorldSpace");
    auto bad      = makeMesh(*renderer, incompatibleLayout());
    auto good     = makeMesh(*renderer, compatibleLayout());

    bool first = true;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit(first ? bad : good, material, Math::Matrix4x4f{1.0f});
            first = false;
        }));
    renderer->start();
    renderer->submitFrame({});
    ASSERT_TRUE(becomesTrue(
        [&]
        {
            return renderer->getState() == Renderer::State::eFailed;
        }));
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 3u);

    ASSERT_TRUE(renderer->stop());
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return RendererTestAccess::pendingUploadCount(*renderer) == 0u;
        }));
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
}

/**
 * Another thread creates and drops textures and materials while the main thread submits frames
 * that draw them. The creator ends without an exception, nothing deadlocks (a hang here ends at
 * the test timeout), and the pending list drains.
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
    renderer->start();

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
            return RendererTestAccess::pendingUploadCount(*renderer) == 0u;
        }));
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
}

TEST_F(RendererTest, startTwiceThrows)
{
    auto renderer = makeRenderer();
    renderer->start();

    EXPECT_THROW(renderer->start(), std::logic_error);
    EXPECT_FALSE(renderer->stop());
}

/**
 * resize() takes effect before the next frame is rendered: the swapchain has the new extent once
 * the first packet after the request has been taken, whether the request came before start() or
 * while the thread ran. A second frame is submitted so that the first has been taken.
 */
TEST_F(RendererTest, resizeRecreatesAtRequestedExtent)
{
    auto renderer = makeRenderer();

    for (const std::uint32_t size : {gResizedExtent, gResizedAgainExtent})
    {
        renderer->resize({.width = size, .height = size});
        renderer->start();
        renderer->submitFrame({});
        renderer->submitFrame({});
        EXPECT_FALSE(renderer->stop());

        // stop() joined the render thread.
        EXPECT_EQ(RendererTestAccess::swapchainExtent(*renderer),
                  (Extent{.width = size, .height = size}));
    }
}

namespace
{
/** RGBA, one byte each: what DrawRecording draws into a target of this format. */
constexpr vk::Format gReadbackFormat = vk::Format::eR8G8B8A8Unorm;

/**
 * Draws @p packet the way the render thread does (the Frame block, the push data, the pipelines,
 * the resources), but into a color target of @p size by @p size pixels instead of a swapchain
 * image, which no test can read, and returns its pixels, rows from the top. The uploads that the
 * renderer holds are recorded first, on the same queue. The renderer must not be running.
 */
std::vector<std::byte>
drawToPixels(Renderer& renderer, const FramePacket& packet, std::uint32_t size)
{
    using namespace Graphics::Vulkan;

    const DevicePtr                        device    = RendererTestAccess::device(renderer);
    const auto                             queue     = device->getGraphicsQueue();
    const auto                             heap      = RendererTestAccess::heap(renderer);
    const auto                             fencePool = FencePool::create(device);
    const auto                             pool      = device->createCommandBufferPool();
    const vk::Extent2D                     extent{.width = size, .height = size};
    const std::vector<GraphicsPipelinePtr> pipelines =
        RendererTestAccess::pipelines(renderer, packet, gReadbackFormat);

    {
        auto recorder = pool->record();
        RendererTestAccess::recordPendingUploads(renderer, recorder, queue->familyIndex);
        auto executable = recorder.finish();
        queue->submit(*fencePool, {.commandBuffers = std::span{&executable, 1u}});
        pool->reset();
    }

    const RenderTargetPtr target = device->createRenderTarget(
        extent,
        gReadbackFormat,
        vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
    const HeapBufferPtr frameBlock = device->createHeapBuffer(heap, sizeof(FrameBlock));
    DrawRecording::writeFrameBlock(*frameBlock, packet, extent);

    const vk::DeviceSize bytes = vk::DeviceSize{size} * size * 4u;
    const BufferPtr      readback =
        device->createBuffer({.size = bytes, .usage = vk::BufferUsageFlagBits::eTransferDst},
                             {.flags = AllocationFlag::eHostSequentialWrite});

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
        auto pass = recorder.beginRendering(
            {{.attachment = target,
              .loadOp     = vk::AttachmentLoadOp::eClear,
              .storeOp    = vk::AttachmentStoreOp::eStore,
              .clearValue = vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}}}});
        DrawRecording::recordDraws(pass, packet, pipelines, frameBlock, extent);
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

    // Read through mapForWrite(), which is meant for writing: legal on lavapipe, whose host
    // memory is coherent.
    const auto mapping = readback->mapForWrite();
    const auto pixels  = mapping.bytes().first(bytes);

    return {pixels.begin(), pixels.end()};
}

/** A quad of side 2 * @p half around the origin in the plane z = 0, two floats of position and
 *  two of uv per vertex, drawn through indices. */
Mesh
makeQuad(Renderer& renderer, float half)
{
    const std::array<float, 16>        vertices{-half,
                                                -half,
                                                0.0f,
                                                0.0f,
                                                -half,
                                                half,
                                                0.0f,
                                                1.0f,
                                                half,
                                                half,
                                                1.0f,
                                                1.0f,
                                                half,
                                                -half,
                                                1.0f,
                                                0.0f};
    const std::array<std::uint32_t, 6> indices{0u, 1u, 2u, 2u, 3u, 0u};

    return renderer.createMesh({.vertices   = std::as_bytes(std::span{vertices}),
                                .indices    = indices,
                                .stride     = gTexturedStride,
                                .attributes = texturedLayout()});
}

/** The camera that looks at the plane z = 0 from the distance of 20, where the near plane is not
 *  in the way. */
CameraView
cameraBeforeThePlane()
{
    constexpr float kDistance = 20.0f;

    return CameraView{.position = {0.0f, 0.0f, -kDistance}};
}

/** The brightest value of a color channel. */
constexpr std::uint8_t gFull = 255u;

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
                                     .width  = 2u,
                                     .height = 2u,
                                     .filter = filter});
        FramePacket packet;
        packet.draws.setCamera(cameraBeforeThePlane());
        packet.draws.submit(makeQuad(*renderer, kHalf),
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

    auto             renderer = makeRenderer();
    const CameraView camera{.position = {10.0f, 20.0f, -30.0f}};
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [camera](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.setCamera(camera);
        }));

    renderer->start();
    for (int frame = 0; frame < kFrames; ++frame)
    {
        // Frame i: the time is 1 + i, the time between frames is (1 + i) / 100.
        renderer->submitFrame(
            {.timeSeconds = 1.0 + frame, .deltaSeconds = static_cast<float>(1.0 + frame) / 100.0f});
    }
    EXPECT_FALSE(renderer->stop());

    const Extent extent = RendererTestAccess::swapchainExtent(*renderer);
    ASSERT_NE(camera.view(), camera.projection(extent));

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
        EXPECT_EQ(block.projection, camera.projection(extent));
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
        auto pass = recorder.beginRendering({{.attachment = target}});
        DrawRecording::recordDraws(pass, packet, {}, frameBlock, extent);
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
        FramePacket packet{.frame = {.timeSeconds = timeSeconds, .deltaSeconds = deltaSeconds}};
        packet.draws.setCamera(cameraBeforeThePlane());
        packet.draws.submit(quad, material, Math::Matrix4x4f{1.0f});

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
} // namespace VOG::Tests
