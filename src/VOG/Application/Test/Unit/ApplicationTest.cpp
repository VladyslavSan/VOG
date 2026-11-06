#include "VOG/Engine/Renderer/RendererImpl.hpp"
#include <VOG/Application/Application.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Math/Math.hpp>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace VOG::Engine
{
/** Test-only window into the renderer, befriended by Renderer, as in the tests of Engine. */
struct RendererTestAccess
{
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
 * Makes every acquire of a swapchain image fail as a lost surface does, by swapping the call in the
 * function table of the device of @p renderer: the render thread gets a vk::SurfaceLostKHRError,
 * the failure that a driver, not the renderer, causes. Make it while the render thread is idle,
 * before the first frame.
 */
class SurfaceLoss
{
public:
    explicit SurfaceLoss(Engine::Renderer& renderer)
        : mTable{Engine::RendererTestAccess::dispatcher(renderer)}
    {
        gRealAcquireNextImage        = mTable.vkAcquireNextImageKHR;
        mTable.vkAcquireNextImageKHR = failAcquireNextImage;
    }

    ~SurfaceLoss() { mTable.vkAcquireNextImageKHR = gRealAcquireNextImage; }

    SurfaceLoss(const SurfaceLoss&)            = delete;
    SurfaceLoss& operator=(const SurfaceLoss&) = delete;

private:
    vk::raii::detail::DeviceDispatcher& mTable;
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

/**
 * One watch frame, at a known place in the loop of run(): after the loop has taken a key press from
 * the event queue and before the frame of that loop. SDL runs the callbacks that another thread
 * queued at the start of each pump, and only those queued before the pump. The callback that
 * pushes the key therefore queues the next one, and waits until it is queued; that one runs in the
 * next pump, and queues itself again while the key is in the queue, which the loop empties one
 * event per pump.
 */
struct PressThenWatchFrame
{
    std::atomic<bool> tickQueued = false;
    /** Set while the expose event is pushed, so that an update can tell the watch frame from the
     *  frame of run(). Main thread only. */
    bool         inTick = false;
    std::jthread queuer;

    /** Queues tick() from another thread, because a queue from the main thread runs at once. Waits
     *  until it is queued. */
    void
    queueTick()
    {
        tickQueued = false;
        queuer     = std::jthread{[this]
                                  {
                                  SDL_RunOnMainThread(&PressThenWatchFrame::tick, this, false);
                                  tickQueued = true;
                                  }};
        while (!tickQueued)
        {
            std::this_thread::yield();
        }
    }

    /** Runs in a pump of run(), on the main thread. */
    static void
    press(void* userdata)
    {
        auto& self = *static_cast<PressThenWatchFrame*>(userdata);

        SDL_Event key{};
        key.type         = SDL_EVENT_KEY_DOWN;
        key.key.scancode = SDL_SCANCODE_W;
        key.key.down     = true;
        SDL_PushEvent(&key);
        self.queueTick();
    }

    /** Runs in a later pump; the watch frame is made when the key is out of the queue. */
    static void
    tick(void* userdata)
    {
        auto& self = *static_cast<PressThenWatchFrame*>(userdata);

        if (SDL_HasEvent(SDL_EVENT_KEY_DOWN))
        {
            self.queueTick();

            return;
        }

        SDL_Event expose{};
        expose.type         = SDL_EVENT_WINDOW_EXPOSED;
        expose.window.data1 = 1;
        self.inTick         = true;
        SDL_PushEvent(&expose);
        self.inTick = false;
    }
};

/**
 * Makes every SDL_PollEvent pump the events, for its lifetime. SDL pumps once per cycle of polls,
 * and ends the cycle at a sentinel event it pushes after the pump. No platform pumps between the
 * events it hands out: this order needs the sentinel off (SDL_HINT_POLL_SENTINEL "0"), which VOG
 * never sets. Without the sentinel each poll pumps, and the pump is where the event watch of a
 * live resize makes its frames.
 */
class EveryPollPumps
{
public:
    EveryPollPumps()
        : mWasEnabled{SDL_EventEnabled(SDL_EVENT_POLL_SENTINEL)}
    {
        SDL_SetEventEnabled(SDL_EVENT_POLL_SENTINEL, false);
    }

    ~EveryPollPumps() { SDL_SetEventEnabled(SDL_EVENT_POLL_SENTINEL, mWasEnabled); }

    EveryPollPumps(const EveryPollPumps&)            = delete;
    EveryPollPumps& operator=(const EveryPollPumps&) = delete;

private:
    bool mWasEnabled;
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
        return {.title    = "VOG Application Test",
                .hidden   = true,
                .renderer = {.shaderPath = VOG_TEST_SHADER_PATH}};
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

    EXPECT_THROW(Application({.renderer = {.shaderPath = ""}}), std::runtime_error);
    EXPECT_THROW(Application({.renderer = {.shaderPath = "/does/not/exist"}}), std::runtime_error);

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
        const std::array<std::byte, 4> pixel{};
        Engine::Renderer&              renderer = raw->getRenderer();
        materialCreated                         = static_cast<bool>(renderer.createMaterial(
            {.shader     = renderer.getShader("Textured"),
             .parameters = {{"tint", Math::Vector4f{1.0f}},
                            {"albedo",
                             renderer.createTexture(
                                 {.pixels = pixel, .extent = {.width = 1u, .height = 1u}})}}}));
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

/**
 * Scene::requestQuit() from a Behaviour ends run() after the frame that called it, and the next
 * run() is not told to quit: it runs frames of its own until it is asked again.
 */
TEST_F(ApplicationRunTest, requestQuitFromABehaviourReturnsRunAndTheNextRunRuns)
{
    constexpr int kFramesPerRun = 3;

    int  updates = 0;
    auto app     = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& /*frame*/)
    {
        if (++updates % kFramesPerRun == 0)
        {
            app.getScene().requestQuit();
        }
    };
    const Watchdog watchdog;

    app.run();
    EXPECT_EQ(updates, kFramesPerRun);
    EXPECT_FALSE(app.getScene().quitRequested());

    app.run();
    EXPECT_EQ(updates, 2 * kFramesPerRun);
    EXPECT_FALSE(app.getScene().quitRequested());
}

/**
 * A posted task runs at the start of a frame, before Scene::update(); a task posted by a task runs
 * in the frame after; and any thread may post.
 */
TEST_F(ApplicationRunTest, aPostedTaskRunsBeforeTheNextUpdateAndATaskOfATaskTheFrameAfter)
{
    std::vector<std::string> log;
    int                      updates = 0;
    auto                     app     = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& /*frame*/)
    {
        log.emplace_back("update");
        if (++updates == 2)
        {
            app.getScene().requestQuit();
        }
    };
    const Watchdog watchdog;

    app.post(
        [&]
        {
            log.emplace_back("first");
            app.post(
                [&]
                {
                    log.emplace_back("second");
                });
        });
    // From another thread, before the run: it runs in the first frame too, behind the first.
    std::thread{[&]
                {
                    app.post(
                        [&]
                        {
                            log.emplace_back("other thread");
                        });
                }}
        .join();

    app.run();

    EXPECT_EQ(log,
              (std::vector<std::string>{"first", "other thread", "update", "second", "update"}));
}

/**
 * A task that throws ends run() with its exception. The task behind it did not run; it runs first
 * at the next run(), before the next update.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ApplicationRunTest, aThrowingTaskEndsRunAndTheTaskBehindItRunsAtTheNextRun)
{
    std::vector<std::string> log;
    auto                     app = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& /*frame*/)
    {
        log.emplace_back("update");
        app.getScene().requestQuit();
    };
    const Watchdog watchdog;

    app.post(
        [&]
        {
            log.emplace_back("throws");
            throw std::runtime_error{"task failed"};
        });
    app.post(
        [&]
        {
            log.emplace_back("behind");
        });

    EXPECT_THROW(app.run(), std::runtime_error);
    EXPECT_EQ(log, (std::vector<std::string>{"throws"}));

    app.run();
    EXPECT_EQ(log, (std::vector<std::string>{"throws", "behind", "update"}));
}

/**
 * The events of the window reach the Behaviours as Input: a key and a mouse button are pressed in
 * the frame after the event, down in the frames after, and released in the one after their event;
 * the position of the mouse is in pixels, and the viewport is the window in pixels. The frames
 * push the events themselves, from update(), so they are in order.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ApplicationRunTest, keyboardAndMouseEventsReachTheBehavioursAsInput)
{
    constexpr float kMouseX = 100.0f;
    constexpr float kMouseY = 50.0f;
    // The frame that pushes the events, the one that pushes their release, and the one that quits.
    constexpr int kPushFrame    = 1;
    constexpr int kReleaseFrame = 3;
    constexpr int kLastFrame    = 5;

    struct Seen
    {
        bool pressedW, downW, releasedW, pressedLeft, downLeft, releasedLeft;
    };

    std::vector<Seen> seen;
    Engine::Extent    viewport;
    Math::Vector2f    mouse{};
    int               frames = 0;
    auto              app    = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& frame)
    {
        using Engine::Key;
        using Engine::MouseButton;

        viewport = frame.viewport;
        mouse    = frame.input.mousePosition();
        seen.push_back({frame.input.wasPressed(Key::eW),
                        frame.input.isDown(Key::eW),
                        frame.input.wasReleased(Key::eW),
                        frame.input.wasPressed(MouseButton::eLeft),
                        frame.input.isDown(MouseButton::eLeft),
                        frame.input.wasReleased(MouseButton::eLeft)});

        SDL_Event event{};
        switch (++frames)
        {
        case kPushFrame:
            event.type         = SDL_EVENT_KEY_DOWN;
            event.key.scancode = SDL_SCANCODE_W;
            event.key.down     = true;
            SDL_PushEvent(&event);
            event               = {};
            event.type          = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down   = true;
            SDL_PushEvent(&event);
            event          = {};
            event.type     = SDL_EVENT_MOUSE_MOTION;
            event.motion.x = kMouseX;
            event.motion.y = kMouseY;
            SDL_PushEvent(&event);
            break;
        case kReleaseFrame:
            event.type         = SDL_EVENT_KEY_UP;
            event.key.scancode = SDL_SCANCODE_W;
            event.key.down     = false;
            SDL_PushEvent(&event);
            event               = {};
            event.type          = SDL_EVENT_MOUSE_BUTTON_UP;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.down   = false;
            SDL_PushEvent(&event);
            break;
        case kLastFrame:
            app.getScene().requestQuit();
            break;
        default:
            break;
        }
    };
    const Watchdog watchdog;

    app.run();

    ASSERT_EQ(seen.size(), static_cast<std::size_t>(kLastFrame));
    // Frame 1 pushes; frame 2 sees the press; frame 3 sees it held; frame 4 sees the release.
    EXPECT_FALSE(seen[0].downW);
    EXPECT_TRUE(seen[1].pressedW && seen[1].downW && !seen[1].releasedW);
    EXPECT_TRUE(seen[1].pressedLeft && seen[1].downLeft && !seen[1].releasedLeft);
    EXPECT_TRUE(!seen[2].pressedW && seen[2].downW && !seen[2].releasedW);
    EXPECT_TRUE(!seen[2].pressedLeft && seen[2].downLeft);
    EXPECT_TRUE(!seen[3].downW && seen[3].releasedW);
    EXPECT_TRUE(!seen[3].downLeft && seen[3].releasedLeft);
    EXPECT_TRUE(!seen[4].releasedW && !seen[4].releasedLeft);

    // The window is 640 by 480 points; its pixels are the points times the density of the display.
    EXPECT_GT(viewport.width, 0u);
    const float density = static_cast<float>(viewport.width) / 640.0f;
    EXPECT_FLOAT_EQ(mouse.x, kMouseX * density);
    EXPECT_FLOAT_EQ(mouse.y, kMouseY * density);
}

/**
 * A key press that run() has taken from the events is in one frame only, also when a watch frame
 * (the expose events of a live resize) comes between the taking and the frame of the loop: the
 * watch frame sees the press, and the frame of the loop does not, though the key is still down.
 * With the poll sentinel on, as VOG runs, no pump falls between the taking and the frame of the
 * loop, so this order does not happen; the test turns the sentinel off to pin the rule for a pump
 * that does come between.
 */
TEST_F(ApplicationRunTest, aPressTakenBeforeAWatchFrameIsSeenOnceAndNotByTheFrameAfter)
{
    struct Seen
    {
        bool watch, pressedW, downW;
    };

    PressThenWatchFrame sequence;
    std::vector<Seen>   seen;
    auto                app = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& frame)
    {
        seen.push_back({sequence.inTick,
                        frame.input.wasPressed(Engine::Key::eW),
                        frame.input.isDown(Engine::Key::eW)});
        // The loop makes one more frame after the watch frame, and then returns.
        if (sequence.inTick)
        {
            app.getScene().requestQuit();
        }
    };
    const EveryPollPumps everyPollPumps;
    const Watchdog       watchdog;
    const std::jthread   starter{
        [&sequence]
        {
            SDL_RunOnMainThread(&PressThenWatchFrame::press, &sequence, false);
        }};

    app.run();

    ASSERT_GE(seen.size(), 2u);
    const Seen& watchFrame = seen[seen.size() - 2];
    const Seen& loopFrame  = seen.back();
    EXPECT_TRUE(watchFrame.watch && watchFrame.pressedW && watchFrame.downW);
    EXPECT_TRUE(!loopFrame.watch && !loopFrame.pressedW && loopFrame.downW);
    EXPECT_EQ(std::ranges::count_if(seen,
                                    [](const Seen& frame)
                                    {
                                        return frame.pressedW;
                                    }),
              1);
}

/** What is held when a run() ends is not held in the next one: run() starts with an empty input. */
TEST_F(ApplicationRunTest, runStartsWithAnEmptyInput)
{
    constexpr int kFramesOfTheFirstRun = 3;

    int  run            = 0;
    int  frames         = 0;
    bool heldAtTheEnd   = false;
    bool heldAtTheStart = true;
    auto app            = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& frame)
    {
        ++frames;
        if (run == 0)
        {
            if (frames == 1)
            {
                SDL_Event key{};
                key.type         = SDL_EVENT_KEY_DOWN;
                key.key.scancode = SDL_SCANCODE_W;
                key.key.down     = true;
                SDL_PushEvent(&key);
            }
            else if (frames == kFramesOfTheFirstRun)
            {
                heldAtTheEnd = frame.input.isDown(Engine::Key::eW);
                app.getScene().requestQuit();
            }

            return;
        }
        heldAtTheStart = frame.input.isDown(Engine::Key::eW);
        app.getScene().requestQuit();
    };
    const Watchdog watchdog;

    app.run();
    ASSERT_TRUE(heldAtTheEnd);

    run    = 1;
    frames = 0;
    app.run();
    EXPECT_FALSE(heldAtTheStart);
}

/**
 * A failure of the render thread that the renderer did not cause (a lost surface at the acquire)
 * ends run() with that exception, and it is final: the next run() rethrows it at once, with no
 * frame. The loss is made in the function table of the device of the renderer, through the friend
 * the renderer has for its tests, so the production API has no way in for it.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(ApplicationRunTest, aRenderThreadFailureIsRethrownByRunAndStaysFinal)
{
    int  updates = 0;
    auto app     = makeApplication();
    app.getScene().createGameObject().addBehaviour<LambdaBehaviour>().onUpdate =
        [&](const Engine::FrameContext& /*frame*/)
    {
        ++updates;
    };
    const SurfaceLoss loss{app.getRenderer()};
    const Watchdog    watchdog;

    EXPECT_THROW(app.run(), vk::SurfaceLostKHRError);
    EXPECT_EQ(app.getRenderer().getState(), Engine::Renderer::State::eFailed);

    const int updatesOfTheFirstRun = updates;
    EXPECT_THROW(app.run(), vk::SurfaceLostKHRError);
    EXPECT_EQ(updates, updatesOfTheFirstRun);
}
} // namespace VOG::Application::Test
