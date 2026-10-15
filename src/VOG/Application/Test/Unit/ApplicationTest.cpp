#include <VOG/Application/Application.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace VOG::Application::Test
{
namespace
{
/** A run() that nothing quits ends after this, with a failure. */
constexpr std::chrono::seconds gDeadline{5};
/** The pause between the expose events of the emulated loop. */
constexpr Uint32 gTickMs = 5;

bool
countEvent(void* count, SDL_Event* /*event*/)
{
    ++*static_cast<int*>(count);

    return true;
}

/** Runs a function for every frame. */
class LambdaRenderable final : public Engine::Renderable
{
public:
    explicit LambdaRenderable(std::function<void()> function)
        : mFunction{std::move(function)}
    {
    }

    void
    onDraw(const Engine::FrameContext& /*frameContext*/, Engine::RenderQueue& /*queue*/) override
    {
        mFunction();
    }

private:
    std::function<void()> mFunction;
};

/** Runs functions for the start, every update and the end of its object. */
class LambdaBehaviour final : public Engine::Behaviour
{
public:
    std::function<void()>                            onStart;
    std::function<void(const Engine::FrameContext&)> onUpdate;
    std::function<void()>                            onEnd;

    void
    start() override
    {
        if (onStart)
        {
            onStart();
        }
    }

    void
    update(const Engine::FrameContext& frame) override
    {
        if (onUpdate)
        {
            onUpdate(frame);
        }
    }

    void
    onDestroy() override
    {
        if (onEnd)
        {
            onEnd();
        }
    }
};

/** Sets a flag when it dies, which is when the renderer that holds it dies. */
class Sentinel final : public Engine::Renderable
{
public:
    explicit Sentinel(bool& gone)
        : mGone{gone}
    {
    }

    ~Sentinel() override { mGone = true; }

    Sentinel(const Sentinel&)            = delete;
    Sentinel& operator=(const Sentinel&) = delete;

    void
    onDraw(const Engine::FrameContext& /*frameContext*/, Engine::RenderQueue& /*queue*/) override
    {
    }

private:
    bool& mGone;
};

/** Asks run() to quit. */
void
pushQuit()
{
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

/**
 * What the OS does while the user drags a window edge on macOS and Windows: the main thread stays
 * in a loop of its own, and the loop sends an expose event about every 5 ms (16 ms in reality).
 * run() cannot submit frames then; the event watch does. The loop ends when @p done holds, or
 * after the deadline, and then asks run() to quit.
 */
struct LiveResize
{
    std::function<bool()> done;
    /** Set while an event is pushed from the loop, so that onDraw can tell a watch frame from the
     *  frame of run(). Main thread only. */
    bool inTick = false;

    static void
    run(void* userdata)
    {
        auto&      self     = *static_cast<LiveResize*>(userdata);
        const auto deadline = std::chrono::steady_clock::now() + gDeadline;
        while (!self.done() && std::chrono::steady_clock::now() < deadline)
        {
            SDL_Event expose{};
            expose.type         = SDL_EVENT_WINDOW_EXPOSED;
            expose.window.data1 = 1;

            self.inTick = true;
            SDL_PushEvent(&expose);
            self.inTick = false;

            SDL_Delay(gTickMs);
        }

        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
};

/** Quits run() after the deadline if the test has not ended it. Declare it after the Application,
 *  so it is joined before the Application goes. */
class Watchdog
{
public:
    Watchdog()
        : mThread{[](const std::stop_token& stopToken)
                  {
                      std::mutex                  mutex;
                      std::condition_variable_any wake;
                      std::unique_lock            lock{mutex};
                      wake.wait_for(lock,
                                    stopToken,
                                    gDeadline,
                                    []
                                    {
                                        return false;
                                    });
                      if (!stopToken.stop_requested())
                      {
                          ADD_FAILURE()
                              << "run() did not end within " << gDeadline.count() << " s.";
                          SDL_Event quit{};
                          quit.type = SDL_EVENT_QUIT;
                          SDL_PushEvent(&quit);
                      }
                  }}
    {
    }

private:
    std::jthread mThread;
};

/** run() needs a window with a Vulkan surface: skipped on the dummy video drivers. */
class ApplicationRunTest : public testing::Test
{
protected:
    void
    SetUp() override
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            GTEST_SKIP() << "No video driver: " << SDL_GetError();
        }
        const std::string_view driver = SDL_GetCurrentVideoDriver();
        const bool             real   = driver != "offscreen" && driver != "dummy";
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        if (!real)
        {
            GTEST_SKIP() << "The video driver \"" << driver << "\" has no window to draw to.";
        }
    }

    static Application::ApplicationConfig
    makeConfig()
    {
        // Hidden, so the tests do not flash windows; a hidden window presents all the same.
        return {.title             = "VOG Application Test",
                .hidden            = true,
                .shaderStoragePath = VOG_TEST_SHADER_PATH};
    }

    static Application
    makeApplication()
    {
        return Application(makeConfig());
    }

    /** Asks the main thread, from another one, to emulate a live resize once run() pumps events. */
    static std::jthread
    startLiveResize(LiveResize& liveResize)
    {
        return std::jthread{[&liveResize]
                            {
                                SDL_RunOnMainThread(&LiveResize::run, &liveResize, false);
                            }};
    }
};
} // namespace

/**
 * A bad config throws before anything touches SDL. The test holds the events subsystem itself.
 * The constructor's own SDL handle would shut SDL down when it unwinds, so SDL staying initialized
 * shows that no handle was ever made, with or without a video driver. Creating a window posts
 * window events, which the event watch counts; only that count needs a video driver to see the
 * difference.
 */
TEST(Application, invalidConfigThrowsBeforeSdlInit)
{
    ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_EVENTS));

    int events = 0;
    SDL_AddEventWatch(countEvent, &events);

    EXPECT_THROW(Application({.shaderStoragePath = ""}), std::runtime_error);
    EXPECT_THROW(Application({.shaderStoragePath = "/does/not/exist"}), std::runtime_error);

    // No window was created (no events), and the handle that would call SDL_Quit never existed.
    EXPECT_EQ(events, 0);
    EXPECT_NE(SDL_WasInit(SDL_INIT_EVENTS), 0u);

    SDL_RemoveEventWatch(countEvent, &events);
    SDL_Quit();
}

/** The watch submits a frame for the expose events of a live resize, which run() never sees. */
TEST_F(ApplicationRunTest, exposeDuringLiveResizeRunsAFrame)
{
    std::atomic<int> watchFrames = 0;
    LiveResize       liveResize{.done = [&]
                                {
                              return watchFrames > 0;
                                }};

    auto app = makeApplication();
    app.addRenderable(std::make_shared<LambdaRenderable>(
        [&]
        {
            if (liveResize.inTick)
            {
                ++watchFrames;
            }
        }));
    const Watchdog     watchdog;
    const std::jthread poster = startLiveResize(liveResize);

    app.run();

    EXPECT_GT(watchFrames, 0);
}

/** An onDraw that pumps events, here by pushing an expose, does not start a frame inside a frame.
 */
TEST_F(ApplicationRunTest, exposeInsideOnDrawDoesNotReenterFrame)
{
    int        depth      = 0;
    int        maxDepth   = 0;
    bool       nestedSent = false;
    LiveResize liveResize{.done = [&]
                          {
                              return nestedSent;
                          }};

    auto app = makeApplication();
    app.addRenderable(std::make_shared<LambdaRenderable>(
        [&]
        {
            maxDepth = std::max(maxDepth, ++depth);
            // Only in a watch frame: the slot is empty there, so a nested frame would not be
            // skipped for want of room.
            if (liveResize.inTick && !nestedSent)
            {
                nestedSent = true;
                SDL_Event expose{};
                expose.type         = SDL_EVENT_WINDOW_EXPOSED;
                expose.window.data1 = 1;
                SDL_PushEvent(&expose);
            }
            --depth;
        }));
    const Watchdog     watchdog;
    const std::jthread poster = startLiveResize(liveResize);

    app.run();

    EXPECT_TRUE(nestedSent);
    EXPECT_EQ(maxDepth, 1);
}

/** Behaviour::update() runs in the frame, so a Behaviour that pumps events does not nest one. */
TEST_F(ApplicationRunTest, exposeInsideBehaviourUpdateDoesNotReenterFrame)
{
    int        depth      = 0;
    int        maxDepth   = 0;
    bool       nestedSent = false;
    LiveResize liveResize{.done = [&]
                          {
                              return nestedSent;
                          }};

    auto app = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& /*frame*/)
    {
        maxDepth = std::max(maxDepth, ++depth);
        // Only in a watch frame, as in exposeInsideOnDrawDoesNotReenterFrame.
        if (liveResize.inTick && !nestedSent)
        {
            nestedSent = true;
            SDL_Event expose{};
            expose.type         = SDL_EVENT_WINDOW_EXPOSED;
            expose.window.data1 = 1;
            SDL_PushEvent(&expose);
        }
        --depth;
    };
    const Watchdog     watchdog;
    const std::jthread poster = startLiveResize(liveResize);

    app.run();

    EXPECT_TRUE(nestedSent);
    EXPECT_EQ(maxDepth, 1);
}

/** The first frame of every run() has no previous frame to measure from. */
TEST_F(ApplicationRunTest, deltaIsZeroOnFirstFrameOfEachRun)
{
    constexpr std::size_t kFramesPerRun = 3;

    std::vector<float> deltas;
    auto               app = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& frame)
    {
        deltas.push_back(frame.deltaSeconds);
        if (deltas.size() % kFramesPerRun == 0)
        {
            pushQuit();
        }
    };
    const Watchdog watchdog;

    app.run();
    app.run();

    ASSERT_EQ(deltas.size(), 2 * kFramesPerRun);
    EXPECT_EQ(deltas[0], 0.0f);
    EXPECT_GT(deltas[1] + deltas[2], 0.0f);
    EXPECT_EQ(deltas[kFramesPerRun], 0.0f);
    EXPECT_GT(deltas[kFramesPerRun + 1] + deltas[kFramesPerRun + 2], 0.0f);
}

/**
 * The destructor of the Application ends the scene while the renderer still exists: an
 * onDestroy() that creates a material gets one. The behaviour reports the renderer already gone
 * (a sentinel in its renderables died) instead of using it, so a failing test does not crash.
 */
TEST_F(ApplicationRunTest, applicationTeardownRunsOnDestroyWhileRendererLives)
{
    bool               sentinelGone     = false;
    bool               rendererGoneSeen = false;
    bool               materialCreated  = false;
    auto               app              = std::make_unique<Application>(makeConfig());
    Application* const raw              = app.get();

    raw->addRenderable(std::make_shared<Sentinel>(sentinelGone));
    auto& behaviour    = raw->getScene().createGameObject().addBehaviour<LambdaBehaviour>();
    behaviour.onUpdate = [](const Engine::FrameContext& /*frame*/)
    {
        pushQuit();
    };
    behaviour.onEnd = [&]
    {
        if (sentinelGone)
        {
            rendererGoneSeen = true;
            return;
        }
        materialCreated = raw->getRenderer().createMaterial({.shader = "WorldSpace"}) != nullptr;
    };
    const Watchdog watchdog;

    raw->run();
    EXPECT_FALSE(sentinelGone);
    app.reset();

    EXPECT_FALSE(rendererGoneSeen);
    EXPECT_TRUE(materialCreated);
    EXPECT_TRUE(sentinelGone);
}

/** The watch cannot throw through SDL: run() rethrows what a watch frame threw, and stops. */
TEST_F(ApplicationRunTest, exceptionFromWatchFrameIsRethrownByRun)
{
    bool       thrown = false;
    LiveResize liveResize{.done = [&]
                          {
                              return thrown;
                          }};

    auto app = makeApplication();
    app.addRenderable(std::make_shared<LambdaRenderable>(
        [&]
        {
            if (liveResize.inTick)
            {
                thrown = true;
                throw std::runtime_error{"onDraw failed in a watch frame"};
            }
        }));
    const Watchdog     watchdog;
    const std::jthread poster = startLiveResize(liveResize);

    EXPECT_THROW(app.run(), std::runtime_error);
}
} // namespace VOG::Application::Test
