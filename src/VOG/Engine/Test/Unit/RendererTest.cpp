#include "VOG/Engine/Renderer/FramePacketQueue.hpp"
#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/PipelineCache.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>
#include <VOG/Graphics/Vulkan/UniformBlock.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
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
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

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

    /** Uploads that createTexture() queued and no submitted frame has recorded yet. */
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

/** Renders into a headless surface, with the validation layer when it is installed. */
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

        mValidationInstalled = Graphics::Vulkan::hasLayer(
            context.enumerateInstanceLayerProperties(), "VK_LAYER_KHRONOS_validation");
    }

    std::unique_ptr<Renderer>
    makeRenderer(const std::filesystem::path& shaderPath = VOG_TEST_SHADER_PATH) const
    {
        Renderer::Config config{
            .appName          = "VOG Renderer Test",
            .engineName       = "Test",
            .extensions       = {"VK_EXT_headless_surface"},
            .framesInFlight   = 2u,
            .shaderSourcePath = shaderPath,
            .extent           = gStartExtent,
        };
        // Errors reach the log; they do not fail a test.
        if (mValidationInstalled)
        {
            config.layers = {"VK_LAYER_KHRONOS_validation"};
        }

        return std::make_unique<Renderer>(
            Common::SurfaceHandle{.platform = Common::SurfaceHandle::Platform::eHeadless}, config);
    }

    bool mValidationInstalled = false;
};

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
/** Bytes of a tint, the parameters of the Textured shader (four floats). */
constexpr std::size_t gTintSize  = 16u;
constexpr std::size_t gPixelSize = 4u;

/** As the Textured shader reads them: a position and a uv, two floats each. */
std::vector<VertexAttribute>
texturedLayout()
{
    return {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat2, sizeOf(VertexFormat::eFloat2)}};
}

MeshPtr
makeTexturedMesh(Renderer& renderer)
{
    return renderer.createMesh({.vertices   = std::vector<std::byte>(3u * gTexturedStride),
                                .stride     = gTexturedStride,
                                .attributes = texturedLayout()});
}

/** A black 2x2 texture. */
TexturePtr
makeTexture(Renderer& renderer)
{
    const std::vector<std::byte> pixels(std::size_t{2u} * 2u * gPixelSize);

    return renderer.createTexture({.pixels = pixels, .width = 2u, .height = 2u});
}

/** The Textured shader with a 16-byte tint and @p texture. */
MaterialPtr
makeTexturedMaterial(Renderer& renderer, TexturePtr texture)
{
    return renderer.createMaterial({.shader     = "Textured",
                                    .parameters = std::vector<std::byte>(gTintSize),
                                    .textures   = {std::move(texture)}});
}

MeshPtr
makeMesh(Renderer& renderer, const std::vector<VertexAttribute>& attributes)
{
    return renderer.createMesh({.vertices   = std::vector<std::byte>(3u * gQuadStride),
                                .stride     = gQuadStride,
                                .attributes = attributes});
}

/** A renderable that draws @p mesh with @p material in every frame. */
std::shared_ptr<Renderable>
drawing(MeshPtr mesh, MaterialPtr material)
{
    return std::make_shared<LambdaRenderable>(
        [mesh = std::move(mesh), material = std::move(material)](const FrameContext& /*context*/,
                                                                 RenderQueue& queue)
        {
            queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
        });
}

/**
 * A shader directory with one shader whose push-constant block is two matrices, 128 bytes: more
 * than the 84 bytes of push data that the renderer pushes.
 */
class WideShaderDirectory
{
public:
    WideShaderDirectory()
        : mPath{std::filesystem::path{testing::TempDir()} / "VogWideShader"}
    {
        std::filesystem::create_directories(mPath);
        std::ofstream{mPath / "Wide.vert.glsl"} << R"(#version 450
layout(push_constant) uniform constants { mat4 first; mat4 second; } pc;
void main() { gl_Position = pc.first * pc.second * vec4(0.0, 0.0, 0.0, 1.0); }
)";
        // The fragment shader of the WorldSpace shader; an absolute path is not made relative.
        std::ofstream{mPath / "config.json"}
            << R"({"shaders": {"Wide": {"vertex": "Wide.vert.glsl", "fragment": ")"
            << (std::filesystem::path{VOG_TEST_SHADER_PATH} /
                "src/ScreenSpacePositionColor.frag.glsl")
                   .generic_string()
            << R"("}}})";
    }

    ~WideShaderDirectory() { std::filesystem::remove_all(mPath); }

    WideShaderDirectory(const WideShaderDirectory&)            = delete;
    WideShaderDirectory& operator=(const WideShaderDirectory&) = delete;

    const std::filesystem::path&
    path() const
    {
        return mPath;
    }

private:
    std::filesystem::path mPath;
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
    auto material = renderer->createMaterial({.shader = "WorldSpace"});
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
    renderer->addRenderable(drawing(makeMesh(*renderer, incompatibleLayout()),
                                    renderer->createMaterial({.shader = "WorldSpace"})));
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
 * of the frame are drawn: the valid draw's pipeline is in the cache afterwards.
 */
TEST_F(RendererTest, drawWithoutMeshOrMaterialIsSkipped)
{
    auto renderer = makeRenderer();
    auto material = renderer->createMaterial({.shader = "WorldSpace"});
    auto mesh     = makeMesh(*renderer, compatibleLayout());

    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            queue.submit(nullptr, nullptr, Math::Matrix4x4f{1.0f});
            queue.submit(mesh, nullptr, Math::Matrix4x4f{1.0f});
            queue.submit(nullptr, material, Math::Matrix4x4f{1.0f});
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

    EXPECT_THROW(renderer->createMesh({.stride = kStride, .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(renderer->createMesh({.vertices = bytes, .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(renderer->createMesh({.vertices   = std::span{bytes}.first(kStride + 1u),
                                       .stride     = kStride,
                                       .attributes = {float2}}),
                 std::invalid_argument);
    EXPECT_THROW(
        renderer->createMesh(
            {.vertices = bytes, .stride = kStride, .attributes = {{VertexFormat::eFloat2, 4u}}}),
        std::invalid_argument);
    EXPECT_THROW(renderer->createMesh({.vertices   = bytes,
                                       .stride     = kStride,
                                       .attributes = std::vector(9u, VertexAttribute{})}),
                 std::invalid_argument);

    const MeshPtr mesh =
        renderer->createMesh({.vertices = bytes, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->vertexCount, 2u);
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
        renderer->createMesh(
            {.vertices = bytes, .indices = quad, .stride = kStride, .attributes = {float2}}),
        std::invalid_argument);
    EXPECT_THROW(
        renderer->createMesh(
            {.vertices = bytes, .indices = pastEnd, .stride = kStride, .attributes = {float2}}),
        std::invalid_argument);

    const MeshPtr indexed = renderer->createMesh(
        {.vertices = bytes, .indices = triangle, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(indexed);
    EXPECT_EQ(indexed->indexCount, 3u);

    const MeshPtr plain =
        renderer->createMesh({.vertices = bytes, .stride = kStride, .attributes = {float2}});
    ASSERT_TRUE(plain);
    EXPECT_EQ(plain->indexCount, 0u);
}

/**
 * The indices follow the vertices at a multiple of 4 bytes, even when the vertices end off it.
 * MoltenVK only draws strides that are a multiple of 4, and no test draws an unaligned stride, so
 * this test is the padding's only coverage.
 */
TEST_F(RendererTest, createMeshAlignsIndicesAfterVertices)
{
    constexpr std::size_t kStride = 10u;
    // 30 bytes of vertices: the indices start at 32.
    std::vector<std::byte> vertices(3u * kStride);
    for (std::size_t i = 0u; i < vertices.size(); ++i)
    {
        vertices[i] = static_cast<std::byte>(i + 1u);
    }
    const std::uint32_t indices[] = {2u, 1u, 0u};

    auto renderer = makeRenderer();

    const MeshPtr mesh = renderer->createMesh({.vertices   = vertices,
                                               .indices    = indices,
                                               .stride     = kStride,
                                               .attributes = {{VertexFormat::eFloat2, 0u}}});
    ASSERT_TRUE(mesh);
    EXPECT_EQ(mesh->indexOffset, 32u);

    const auto mapping = mesh->buffer->mapForWrite();
    ASSERT_GE(mapping.size, mesh->indexOffset + sizeof(indices));
    EXPECT_EQ(std::memcmp(mapping.data, vertices.data(), vertices.size()), 0);
    EXPECT_EQ(std::memcmp(mapping.data + mesh->indexOffset, indices, sizeof(indices)), 0);
}

/** An indexed mesh reaches the render thread; the validation run of the gate sees the draw. */
TEST_F(RendererTest, indexedMeshIsDrawn)
{
    const std::uint32_t indices[] = {0u, 1u, 2u};

    auto renderer = makeRenderer();
    auto mesh     = renderer->createMesh({.vertices   = std::vector<std::byte>(3u * gQuadStride),
                                          .indices    = indices,
                                          .stride     = gQuadStride,
                                          .attributes = compatibleLayout()});
    renderer->addRenderable(drawing(mesh, renderer->createMaterial({.shader = "WorldSpace"})));
    renderer->start();
    for (int frame = 0; frame < 3; ++frame)
    {
        renderer->submitFrame({});
    }

    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
    EXPECT_EQ(RendererTestAccess::pipelineCount(*renderer), 1u);
}

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMaterialChecksShaderTexturesAndParameterSize)
{
    auto renderer = makeRenderer();

    EXPECT_THROW(renderer->createMaterial({.shader = "NoSuchShader"}), std::invalid_argument);

    // The parameters are a uniform block, not push constants: size 0 to 16384 bytes are fine.
    for (const std::size_t size :
         {std::size_t{0u}, std::size_t{4u}, std::size_t{65u}, gMaxParametersSize})
    {
        EXPECT_TRUE(renderer->createMaterial(
            {.shader = "WorldSpace", .parameters = std::vector<std::byte>(size)}))
            << size << " bytes";
    }
    EXPECT_THROW(
        renderer->createMaterial({.shader     = "WorldSpace",
                                  .parameters = std::vector<std::byte>(gMaxParametersSize + 1u)}),
        std::invalid_argument);

    // At most four textures, none null.
    const TexturePtr texture = makeTexture(*renderer);
    EXPECT_TRUE(renderer->createMaterial(
        {.shader = "Textured", .textures = std::vector<TexturePtr>(gMaxTextures, texture)}));
    EXPECT_THROW(
        renderer->createMaterial({.shader   = "Textured",
                                  .textures = std::vector<TexturePtr>(gMaxTextures + 1u, texture)}),
        std::invalid_argument);
    EXPECT_THROW(renderer->createMaterial({.shader = "Textured", .textures = {texture, nullptr}}),
                 std::invalid_argument);

    // The shader reads 128 bytes of push constants, more than the 84 the renderer pushes, however
    // many parameters the material has.
    const WideShaderDirectory wide;
    auto                      wideRenderer = makeRenderer(wide.path());
    EXPECT_THROW(wideRenderer->createMaterial({.shader = "Wide"}), std::invalid_argument);
    EXPECT_THROW(
        wideRenderer->createMaterial({.shader = "Wide", .parameters = std::vector<std::byte>(64u)}),
        std::invalid_argument);
}

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createTextureRejectsMalformedDescription)
{
    auto renderer = makeRenderer();

    const std::vector<std::byte> pixels(std::size_t{2u} * 3u * gPixelSize);
    EXPECT_NO_THROW(renderer->createTexture({.pixels = pixels, .width = 2u, .height = 3u}));
    EXPECT_NO_THROW(renderer->createTexture(
        {.pixels = pixels, .width = 2u, .height = 3u, .format = TextureFormat::eRGBA8Unorm}));

    EXPECT_THROW(renderer->createTexture({.width = 2u, .height = 3u}), std::invalid_argument);
    // No pixels for no extent are still no texture.
    EXPECT_THROW(renderer->createTexture({}), std::invalid_argument);
    EXPECT_THROW(renderer->createTexture({.pixels = pixels, .width = 0u, .height = 3u}),
                 std::invalid_argument);
    EXPECT_THROW(renderer->createTexture({.pixels = pixels, .width = 2u, .height = 0u}),
                 std::invalid_argument);
    // One byte too few, one byte too many, and the right size for the other extent.
    EXPECT_THROW(
        renderer->createTexture(
            {.pixels = std::span{pixels}.first(pixels.size() - 1u), .width = 2u, .height = 3u}),
        std::invalid_argument);
    EXPECT_THROW(renderer->createTexture({.pixels = pixels, .width = 2u, .height = 2u}),
                 std::invalid_argument);
    EXPECT_THROW(renderer->createTexture({.pixels = pixels, .width = 6u, .height = 6u}),
                 std::invalid_argument);

    // Only the two valid calls left an upload.
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 2u);
}

/**
 * A frame in flight keeps the mesh's buffer alive after the renderer let go of the mesh: the frame
 * that drew it is rendered and gone, but its slot is not reused yet. Once the slot is reused,
 * nothing holds the buffer.
 *
 * A popped packet is rendered unless the acquire skips it; on a headless surface only an acquire
 * timeout does, and the first frames find a free image. Submitting N+1 returns only once N has been
 * popped. The test submits no third frame, and reusing the slot of N takes one (the renderer has
 * two frame slots).
 *
 * It proves that Engine records the buffer through the retaining path and the renderer holds
 * nothing on top. GPU safety rests on the Graphics retention tests; validation errors do not fail
 * a test here.
 */
TEST_F(RendererTest, meshBufferOutlivesMeshWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    std::weak_ptr<const Mesh>               weakMesh;
    std::weak_ptr<Graphics::Vulkan::Buffer> weakBuffer;
    {
        MeshPtr mesh = makeMesh(*renderer, compatibleLayout());
        weakMesh     = mesh;
        weakBuffer   = mesh->buffer;
        renderer->addRenderable(std::make_shared<LambdaRenderable>(
            [mesh = std::move(mesh), material = renderer->createMaterial({.shader = "WorldSpace"})](
                const FrameContext& /*context*/, RenderQueue& queue) mutable
            {
                // The first frame draws the mesh and lets go of it; later frames draw nothing.
                queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
                mesh.reset();
            }));
    }

    renderer->start();
    renderer->submitFrame({});
    renderer->submitFrame({});
    EXPECT_FALSE(renderer->stop());

    EXPECT_TRUE(weakMesh.expired());
    EXPECT_FALSE(weakBuffer.expired());

    // Frames that draw nothing cycle through the slots until the first frame's slot is reused; the
    // deadline keeps a leak from hanging the test.
    renderer->start();
    const auto deadline = std::chrono::steady_clock::now() + gDeadline;
    while (!weakBuffer.expired() && std::chrono::steady_clock::now() < deadline)
    {
        renderer->submitFrame({});
    }
    EXPECT_TRUE(weakBuffer.expired());
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

    TexturePtr texture     = makeTexture(*renderer);
    auto       weakTexture = std::weak_ptr<const Texture>{texture};
    auto       weakImage   = std::weak_ptr<Graphics::Vulkan::Texture>{texture->texture};
    auto       weakStaging = RendererTestAccess::pendingStaging(*renderer, 0u);

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
            mesh.reset();
            material.reset();
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

    std::weak_ptr<Graphics::Vulkan::UniformBlock> weakBlock;
    {
        MaterialPtr material = renderer->createMaterial(
            {.shader = "WorldSpace", .parameters = std::vector<std::byte>(gTintSize)});
        weakBlock = material->block;
        renderer->addRenderable(std::make_shared<LambdaRenderable>(
            [mesh = makeMesh(*renderer, compatibleLayout()), material = std::move(material)](
                const FrameContext& /*context*/, RenderQueue& queue) mutable
            {
                queue.submit(mesh, material, Math::Matrix4x4f{1.0f});
                mesh.reset();
                material.reset();
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
 * A frame that is dropped does not consume the uploads: a zero-sized window ends each frame
 * before it records anything, and the upload is still pending. Once the window has a size again,
 * the next frame records it and the list drains.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, textureUploadSurvivesDroppedFrame)
{
    auto renderer = makeRenderer();
    auto texture  = makeTexture(*renderer);

    renderer->resize({});
    renderer->start();
    // The second call returns only after the render thread took, and dropped, the first frame.
    renderer->submitFrame({});
    renderer->submitFrame({});
    EXPECT_FALSE(renderer->stop());
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 1u);

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
 * The staging buffer lives in the pending list until the frame that recorded the upload has been
 * submitted, then in that frame's command buffer, and is gone once the frame's slot is reused.
 */
TEST_F(RendererTest, textureUploadIsRecordedAndStagingReleased)
{
    auto renderer = makeRenderer();
    auto texture  = makeTexture(*renderer);
    auto staging  = RendererTestAccess::pendingStaging(*renderer, 0u);
    EXPECT_FALSE(staging.expired());

    // Stops at the first frame after which the list is empty, before the frame slot of the upload
    // can be reused.
    renderer->start();
    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return RendererTestAccess::pendingUploadCount(*renderer) == 0u;
        }));
    EXPECT_FALSE(staging.expired());

    EXPECT_TRUE(becomesTrue(
        [&]
        {
            renderer->submitFrame({});
            return staging.expired();
        }));
    EXPECT_FALSE(renderer->stop());
}

/**
 * A render thread that fails in resolvePipelines, before it records anything, leaves the uploads
 * pending, and they are recorded after stop() and start().
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, pendingUploadsSurviveRenderThreadFailureAndRestart)
{
    auto renderer = makeRenderer();
    auto texture  = makeTexture(*renderer);
    auto material = renderer->createMaterial({.shader = "WorldSpace"});
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
    EXPECT_EQ(RendererTestAccess::pendingUploadCount(*renderer), 1u);

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

    std::mutex  currentMutex;
    MaterialPtr current;
    renderer->addRenderable(std::make_shared<LambdaRenderable>(
        [&](const FrameContext& /*context*/, RenderQueue& queue)
        {
            MaterialPtr material;
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
                                    MaterialPtr material =
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
        current.reset();
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
} // namespace VOG::Tests
