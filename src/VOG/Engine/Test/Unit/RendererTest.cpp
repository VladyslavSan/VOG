#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <latch>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace VOG::Engine;

namespace VOG::Engine
{
/** Test-only window into the renderer, befriended by Renderer. */
struct RendererTestAccess
{
    /** On the render thread, or while none runs. */
    static Extent
    swapchainExtent(const Renderer& renderer)
    {
        const vk::Extent2D extent = renderer.mSwapchain->getExtent();

        return {.width = extent.width, .height = extent.height};
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

/** Does nothing but count frames. */
class CountingRenderable : public Renderable
{
public:
    explicit CountingRenderable(std::latch& firstFrame)
        : mFirstFrame{firstFrame}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        // count_down() past zero is undefined, and collect() runs every frame.
        if (!mCounted)
        {
            mCounted = true;
            mFirstFrame.count_down();
        }
    }

private:
    std::latch& mFirstFrame;
    bool        mCounted = false;
};

struct Blocking : Renderable
{
    /** One parameter: three references of two types would be easy to swap. */
    struct Signals
    {
        std::latch&      entered;
        std::latch&      release;
        std::thread::id& destroyedOn;
    };

    explicit Blocking(const Signals& signals)
        : entered{signals.entered}
        , release{signals.release}
        , destroyedOn{signals.destroyedOn}
    {
    }

    ~Blocking() override { destroyedOn = std::this_thread::get_id(); }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        // Only the first frame signals and blocks: count_down() past zero is undefined, and
        // a second frame can run before the dropper requests the stop.
        if (!blocked)
        {
            blocked = true;
            entered.count_down();
            release.wait();
        }
    }

    bool             blocked = false;
    std::latch&      entered;
    std::latch&      release;
    std::thread::id& destroyedOn;
};

/** Throws from its first collect() only, so a restart can get past it. */
struct Failing : Renderable
{
    explicit Failing(std::latch& entered)
        : entered{entered}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        if (thrown)
        {
            return;
        }

        // Before the throw, so stop() cannot win the race against the first frame.
        thrown = true;
        entered.count_down();
        throw std::runtime_error{"collect failed"};
    }

    bool        thrown = false;
    std::latch& entered;
};

struct Child : Renderable
{
    Child(std::vector<std::string>& log, std::latch& firstCollect)
        : log{log}
        , firstCollect{firstCollect}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        // Only the first frame logs and counts down: count_down() past zero is undefined.
        if (!collected)
        {
            collected = true;
            log.emplace_back("child.collect");
            firstCollect.count_down();
        }
    }

    bool                      collected = false;
    std::vector<std::string>& log;
    std::latch&               firstCollect;
};

struct Parent : Renderable
{
    Parent(Renderer& renderer, std::vector<std::string>& log, std::latch& childCollect)
        : renderer{renderer}
        , log{log}
        , childCollect{childCollect}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        if (!collected)
        {
            collected = true;
            log.emplace_back("parent.collect");
            renderer.addRenderable(std::make_shared<Child>(log, childCollect));
        }
    }

    bool                      collected = false;
    Renderer&                 renderer;
    std::vector<std::string>& log;
    std::latch&               childCollect;
};

struct Malformed : Renderable
{
    explicit Malformed(std::latch& entered)
        : entered{entered}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& draws) override
    {
        // Before the push, so stop() cannot win the race against the first frame.
        entered.count_down();
        draws.push_back(Draw{});
    }

    std::latch& entered;
};

/** Records the swapchain extent each collect() sees; it runs on the render thread, which owns the
 *  swapchain. */
struct Probe : Renderable
{
    explicit Probe(const Renderer& renderer)
        : renderer{renderer}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& /*draws*/) override
    {
        extents.push_back(RendererTestAccess::swapchainExtent(renderer));
        if (extents.size() == 1u)
        {
            entered.count_down();
            release.wait();
        }
        else if (extents.size() == 2u)
        {
            second.count_down();
        }
    }

    const Renderer&     renderer;
    std::vector<Extent> extents;
    std::latch          entered{1};
    std::latch          release{1};
    std::latch          second{1};
};

/**
 * Draws its mesh in the first frame. In the second it drops the mesh and blocks in collect(), so
 * the test looks at the first frame's command buffer while it is still held by its frame slot.
 */
struct MeshDropper : Renderable
{
    MeshDropper(MeshPtr mesh, MaterialPtr material)
        : mesh{std::move(mesh)}
        , material{std::move(material)}
    {
    }

    void
    collect(const FrameContext& /*frameContext*/, DrawList& draws) override
    {
        ++collects;
        if (collects == 1u)
        {
            draws.push_back({.mesh = mesh, .material = material});
        }
        else if (collects == 2u)
        {
            mesh.reset();
            entered.count_down();
            release.wait();
        }
    }

    std::size_t collects = 0u;
    MeshPtr     mesh;
    MaterialPtr material;
    std::latch  entered{1};
    std::latch  release{1};
};

/** A shader directory with one shader whose push-constant block is two matrices, 128 bytes. */
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
            << (std::filesystem::path{VOG_TEST_SHADER_PATH} / "src/TestUniformBuffer.frag.glsl")
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
 * Dropping the renderer from another thread while a frame is in flight waits for that frame and
 * tears the renderer down on the dropping thread: the render thread owns nothing, so it cannot
 * be the one that runs ~Renderer. The renderable it was drawing dies there too.
 *
 * It proves liveness and thread affinity. The self-join it is named for can no longer be
 * written, so there is no broken version of the code for it to fail against.
 */
TEST_F(RendererTest, destroyWhileFrameInFlightJoins)
{
    std::latch      entered{1};
    std::latch      release{1};
    std::thread::id destroyedOn;

    auto renderable = std::make_shared<Blocking>(
        Blocking::Signals{.entered = entered, .release = release, .destroyedOn = destroyedOn});
    const std::weak_ptr<Renderable> weak = renderable;

    auto renderer = makeRenderer();
    renderer->addRenderable(std::move(renderable));
    renderer->start();
    entered.wait();

    std::thread           dropper{[&renderer]
                                  {
                            renderer.reset();
                                  }};
    const std::thread::id dropperId = dropper.get_id();
    release.count_down();
    dropper.join();

    EXPECT_TRUE(weak.expired());
    EXPECT_EQ(destroyedOn, dropperId);
}

/**
 * A collect() that throws ends the render thread, and stop() hands the exception to the caller.
 * The renderables stay: after a restart the one that threw is collected again, and so is the one
 * queued behind it.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, failingCollectIsReportedAndRestartable)
{
    std::latch entered{1};
    std::latch firstFrame{1};

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<Failing>(entered));
    renderer->addRenderable(std::make_shared<CountingRenderable>(firstFrame));
    renderer->start();
    entered.wait();

    while (renderer->getState() == Renderer::State::eRunning)
    {
        std::this_thread::yield();
    }
    EXPECT_EQ(renderer->getState(), Renderer::State::eFailed);

    const std::exception_ptr failure = renderer->stop();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), std::runtime_error);
    EXPECT_EQ(renderer->getState(), Renderer::State::eStopped);

    renderer->start();
    firstFrame.wait();
    EXPECT_EQ(renderer->getState(), Renderer::State::eRunning);
    EXPECT_FALSE(renderer->stop());
}

/**
 * A renderable may queue another from collect(): no lock is held while it runs. The child is
 * adopted at the start of the next frame, and collected in it.
 */
TEST_F(RendererTest, addRenderableFromCollectIsCollectedNextFrame)
{
    std::vector<std::string> log;
    std::latch               childCollect{1};

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<Parent>(*renderer, log, childCollect));
    renderer->start();
    childCollect.wait();
    EXPECT_FALSE(renderer->stop());

    // The render thread wrote the log; stop() joined it.
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "parent.collect");
    EXPECT_EQ(log[1], "child.collect");
}

/**
 * A draw without a mesh or a material is a bug of the renderable: it fails the render thread
 * instead of being skipped. It is found while no image is acquired.
 */
TEST_F(RendererTest, malformedDrawFailsRenderThread)
{
    std::latch entered{1};

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<Malformed>(entered));
    renderer->start();
    entered.wait();

    const std::exception_ptr failure = renderer->stop();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), std::invalid_argument);
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

// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(RendererTest, createMaterialRejectsUnknownShaderAndShortParameters)
{
    auto renderer = makeRenderer();

    EXPECT_THROW(renderer->createMaterial({.shader = "NoSuchShader"}), std::invalid_argument);
    EXPECT_THROW(renderer->createMaterial(
                     {.shader = "WorldSpace", .parameters = std::vector<std::byte>(65u)}),
                 std::invalid_argument);
    // The parameters need no range of the shader to read them.
    EXPECT_TRUE(renderer->createMaterial(
        {.shader = "WorldSpace", .parameters = std::vector<std::byte>(4u)}));

    // The shader reads 128 bytes of push constants, the matrix and 64 bytes of parameters.
    const WideShaderDirectory wide;
    auto                      wideRenderer = makeRenderer(wide.path());
    EXPECT_THROW(wideRenderer->createMaterial({.shader = "Wide"}), std::invalid_argument);
    EXPECT_THROW(
        wideRenderer->createMaterial({.shader = "Wide", .parameters = std::vector<std::byte>(63u)}),
        std::invalid_argument);
    EXPECT_TRUE(wideRenderer->createMaterial(
        {.shader = "Wide", .parameters = std::vector<std::byte>(64u)}));
}

/**
 * A frame in flight keeps the mesh's buffer alive after the renderer let go of the mesh: the draw
 * is gone when the next collect() starts, the first frame's slot is not reused yet. Once the slot
 * is reused, nothing holds the buffer.
 *
 * It proves that Engine records the buffer through the retaining path and the renderer holds
 * nothing on top. GPU safety rests on the Graphics retention tests; validation errors do not fail
 * a test here.
 */
TEST_F(RendererTest, meshBufferOutlivesMeshWhileFrameInFlight)
{
    auto renderer = makeRenderer();

    std::shared_ptr<MeshDropper>            dropper;
    std::weak_ptr<const Mesh>               weakMesh;
    std::weak_ptr<Graphics::Vulkan::Buffer> weakBuffer;
    {
        constexpr std::size_t kStride = 24u;
        MeshPtr               mesh    = renderer->createMesh(
            {.vertices   = std::vector<std::byte>(3u * kStride),
             .stride     = kStride,
             .attributes = {{VertexFormat::eFloat2, 0u},
                            {VertexFormat::eFloat4, sizeOf(VertexFormat::eFloat2)}}});
        weakMesh   = mesh;
        weakBuffer = mesh->buffer;
        dropper = std::make_shared<MeshDropper>(std::move(mesh),
                                                renderer->createMaterial({.shader = "WorldSpace"}));
    }

    renderer->addRenderable(dropper);
    renderer->start();
    dropper->entered.wait();
    EXPECT_TRUE(weakMesh.expired());
    EXPECT_FALSE(weakBuffer.expired());

    // The next frame reuses the first frame's slot; the deadline keeps a leak from hanging the
    // test.
    dropper->release.count_down();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (!weakBuffer.expired() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    EXPECT_TRUE(weakBuffer.expired());
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
 * resize() takes effect before the next frame collects: the swapchain has the new extent when that
 * frame's collect() runs, not only after an out-of-date acquire or at the end of the frame. A
 * resize requested before start() shows in the first collect(); one requested while the renderable
 * is blocked inside the first frame shows in the second.
 */
TEST_F(RendererTest, resizeRecreatesAtRequestedExtent)
{
    auto renderer = makeRenderer();
    auto probe    = std::make_shared<Probe>(*renderer);
    renderer->addRenderable(probe);
    renderer->resize({.width = gResizedExtent, .height = gResizedExtent});
    renderer->start();

    probe->entered.wait();
    renderer->resize({.width = gResizedAgainExtent, .height = gResizedAgainExtent});
    probe->release.count_down();
    probe->second.wait();
    EXPECT_FALSE(renderer->stop());

    // The render thread wrote the extents; stop() joined it.
    ASSERT_GE(probe->extents.size(), 2u);
    EXPECT_EQ(probe->extents[0], (Extent{.width = gResizedExtent, .height = gResizedExtent}));
    EXPECT_EQ(probe->extents[1],
              (Extent{.width = gResizedAgainExtent, .height = gResizedAgainExtent}));
}
} // namespace VOG::Tests
