#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <exception>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace VOG::Engine;

namespace VOG::Tests
{
namespace
{
constexpr vk::Extent2D gStartExtent{.width = 64u, .height = 64u};

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
    makeRenderer() const
    {
        Renderer::Config config{
            .appName          = "VOG Renderer Test",
            .engineName       = "Test",
            .extensions       = {"VK_EXT_headless_surface"},
            .framesInFlight   = 2u,
            .shaderSourcePath = VOG_TEST_SHADER_PATH,
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
    prepare(const ResourceContext&) override
    {
    }

    void
    collect(const FrameContext&, std::vector<RenderItem>&) override
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
} // namespace

/**
 * Dropping the renderer from another thread while a frame is in flight waits for that frame and
 * tears the renderer down on the dropping thread: the render thread owns nothing, so it cannot
 * be the one that runs ~Renderer. The renderable it was drawing dies there too.
 */
TEST_F(RendererTest, destroyWhileFrameInFlightJoins)
{
    struct Blocking : Renderable
    {
        Blocking(std::latch& entered, std::latch& release, std::thread::id& destroyedOn)
            : entered{entered}
            , release{release}
            , destroyedOn{destroyedOn}
        {
        }

        ~Blocking() override { destroyedOn = std::this_thread::get_id(); }

        void
        prepare(const ResourceContext&) override
        {
        }

        void
        collect(const FrameContext&, std::vector<RenderItem>&) override
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

    std::latch      entered{1};
    std::latch      release{1};
    std::thread::id destroyedOn;

    auto renderable = std::make_shared<Blocking>(entered, release, destroyedOn);
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
 * A prepare() that throws ends the render thread, and stop() hands the exception to the caller.
 * Only the renderable that threw is lost: the one queued behind it is prepared after a restart.
 */
TEST_F(RendererTest, failingPrepareIsReportedAndRestartable)
{
    struct Failing : Renderable
    {
        explicit Failing(std::latch& entered)
            : entered{entered}
        {
        }

        void
        prepare(const ResourceContext&) override
        {
            // Before the throw, so stop() cannot win the race against the first frame.
            entered.count_down();
            throw std::runtime_error{"prepare failed"};
        }

        void
        collect(const FrameContext&, std::vector<RenderItem>&) override
        {
        }

        std::latch& entered;
    };

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
 * A renderable may queue another from prepare(): no lock is held while it runs. The child is
 * taken by the same drain, so it is prepared in the same frame, before the first collect().
 */
TEST_F(RendererTest, addRenderableFromPrepareIsPreparedInTheSameFrame)
{
    std::vector<std::string> log;
    std::latch               firstCollect{1};

    struct Child : Renderable
    {
        explicit Child(std::vector<std::string>& log)
            : log{log}
        {
        }

        void
        prepare(const ResourceContext&) override
        {
            log.emplace_back("child.prepare");
        }

        void
        collect(const FrameContext&, std::vector<RenderItem>&) override
        {
        }

        std::vector<std::string>& log;
    };

    struct Parent : Renderable
    {
        Parent(Renderer& renderer, std::vector<std::string>& log, std::latch& firstCollect)
            : renderer{renderer}
            , log{log}
            , firstCollect{firstCollect}
        {
        }

        void
        prepare(const ResourceContext&) override
        {
            log.emplace_back("parent.prepare");
            renderer.addRenderable(std::make_shared<Child>(log));
        }

        void
        collect(const FrameContext&, std::vector<RenderItem>&) override
        {
            // Only the first frame logs and counts down: count_down() past zero is undefined.
            if (!collected)
            {
                collected = true;
                log.emplace_back("parent.collect");
                firstCollect.count_down();
            }
        }

        bool                      collected = false;
        Renderer&                 renderer;
        std::vector<std::string>& log;
        std::latch&               firstCollect;
    };

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<Parent>(*renderer, log, firstCollect));
    renderer->start();
    firstCollect.wait();
    EXPECT_FALSE(renderer->stop());

    // The render thread wrote the log; stop() joined it.
    ASSERT_GE(log.size(), 3u);
    EXPECT_EQ(log[0], "parent.prepare");
    EXPECT_EQ(log[1], "child.prepare");
    EXPECT_EQ(log[2], "parent.collect");
}

/**
 * An item without a pipeline, buffer or vertex count is a bug of the renderable: it fails the
 * render thread instead of being skipped. It is found while no image is acquired.
 */
TEST_F(RendererTest, malformedRenderItemFailsRenderThread)
{
    struct Malformed : Renderable
    {
        explicit Malformed(std::latch& entered)
            : entered{entered}
        {
        }

        void
        prepare(const ResourceContext&) override
        {
        }

        void
        collect(const FrameContext&, std::vector<RenderItem>& renderItems) override
        {
            // Before the push, so stop() cannot win the race against the first frame.
            entered.count_down();
            renderItems.push_back(RenderItem{});
        }

        std::latch& entered;
    };

    std::latch entered{1};

    auto renderer = makeRenderer();
    renderer->addRenderable(std::make_shared<Malformed>(entered));
    renderer->start();
    entered.wait();

    const std::exception_ptr failure = renderer->stop();
    ASSERT_TRUE(failure);
    EXPECT_THROW(std::rethrow_exception(failure), std::invalid_argument);
}

TEST_F(RendererTest, startTwiceThrows)
{
    auto renderer = makeRenderer();
    renderer->start();

    EXPECT_THROW(renderer->start(), std::logic_error);
    EXPECT_FALSE(renderer->stop());
}

/**
 * resize() takes effect on the very next frame: that frame's FrameContext has the new extent.
 * The renderable blocks inside the first frame, so the resize lands between two frames.
 */
TEST_F(RendererTest, resizeRecreatesAtRequestedExtent)
{
    struct Probe : Renderable
    {
        void
        prepare(const ResourceContext&) override
        {
        }

        void
        collect(const FrameContext& frameContext, std::vector<RenderItem>&) override
        {
            extents.push_back(frameContext.extent);
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

        std::vector<vk::Extent2D> extents;
        std::latch                entered{1};
        std::latch                release{1};
        std::latch                second{1};
    };

    auto probe    = std::make_shared<Probe>();
    auto renderer = makeRenderer();
    renderer->addRenderable(probe);
    renderer->start();

    probe->entered.wait();
    renderer->resize({.width = 32u, .height = 32u});
    probe->release.count_down();
    probe->second.wait();
    EXPECT_FALSE(renderer->stop());

    // The render thread wrote the extents; stop() joined it.
    ASSERT_GE(probe->extents.size(), 2u);
    EXPECT_EQ(probe->extents[0].width, 64u);
    EXPECT_EQ(probe->extents[0].height, 64u);
    EXPECT_EQ(probe->extents[1].width, 32u);
    EXPECT_EQ(probe->extents[1].height, 32u);
}
} // namespace VOG::Tests
