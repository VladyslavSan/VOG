#include "VOG/Application/Application.hpp"

#include <VOG/Application/SDL.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <format>
#include <stdexcept>
#include <utility>

namespace VOG::Application
{
namespace
{
void
validateConfig(const Application::ApplicationConfig& config)
{
    if (config.renderer.shaderPath.empty())
    {
        throw std::runtime_error("Shader storage path is empty.");
    }
    if (!std::filesystem::exists(config.renderer.shaderPath))
    {
        throw std::runtime_error(std::format("Shader storage path \"{}\" does not exist.",
                                             config.renderer.shaderPath.string()));
    }
}

/** SDL reports sizes as signed integers. */
std::uint32_t
toPixels(Sint32 size)
{
    return static_cast<std::uint32_t>(std::max(size, Sint32{0}));
}

/** The key of an SDL scancode, if a game reads it. */
std::optional<Engine::Key>
keyOf(SDL_Scancode scancode)
{
    using Engine::Key;

    if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
    {
        return static_cast<Key>(std::to_underlying(Key::eA) + (scancode - SDL_SCANCODE_A));
    }
    if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
    {
        return static_cast<Key>(std::to_underlying(Key::e1) + (scancode - SDL_SCANCODE_1));
    }

    switch (scancode)
    {
    case SDL_SCANCODE_0:
        return Key::e0;
    case SDL_SCANCODE_SPACE:
        return Key::eSpace;
    case SDL_SCANCODE_ESCAPE:
        return Key::eEscape;
    case SDL_SCANCODE_RETURN:
        return Key::eReturn;
    case SDL_SCANCODE_LEFT:
        return Key::eLeft;
    case SDL_SCANCODE_RIGHT:
        return Key::eRight;
    case SDL_SCANCODE_UP:
        return Key::eUp;
    case SDL_SCANCODE_DOWN:
        return Key::eDown;
    case SDL_SCANCODE_LSHIFT:
        return Key::eLeftShift;
    default:
        return std::nullopt;
    }
}

std::optional<Engine::MouseButton>
mouseButtonOf(Uint8 button)
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return Engine::MouseButton::eLeft;
    case SDL_BUTTON_RIGHT:
        return Engine::MouseButton::eRight;
    case SDL_BUTTON_MIDDLE:
        return Engine::MouseButton::eMiddle;
    default:
        return std::nullopt;
    }
}

/** Sets a flag for its lifetime. */
class FlagScope
{
public:
    explicit FlagScope(bool& flag)
        : mFlag{flag}
    {
        mFlag = true;
    }

    ~FlagScope() { mFlag = false; }

    FlagScope(const FlagScope&)            = delete;
    FlagScope& operator=(const FlagScope&) = delete;

private:
    bool& mFlag;
};

/** An SDL event watch for its lifetime. */
class EventWatch
{
public:
    EventWatch(SDL_EventFilter filter, void* userdata)
        : mFilter{filter}
        , mUserdata{userdata}
    {
        if (!SDL_AddEventWatch(mFilter, mUserdata))
        {
            throw std::runtime_error(std::format("SDL_AddEventWatch failed: {}", SDL_GetError()));
        }
    }

    ~EventWatch() { SDL_RemoveEventWatch(mFilter, mUserdata); }

    EventWatch(const EventWatch&)            = delete;
    EventWatch& operator=(const EventWatch&) = delete;

private:
    SDL_EventFilter mFilter;
    void*           mUserdata;
};

/** Logs a render-thread failure that run() cannot rethrow, worded like ~Renderer. */
void
logFailure(const std::exception_ptr& failure)
{
    try
    {
        std::rethrow_exception(failure);
    }
    catch (const std::exception& exception)
    {
        spdlog::error("The render thread failed while run() was unwinding: {}", exception.what());
    }
    catch (...)
    {
        spdlog::error(
            "The render thread failed with an unknown exception while run() was "
            "unwinding.");
    }
}
} // namespace

Application::Application(const ApplicationConfig& config)
    : Application{config, (validateConfig(config), Validated{})}
{
}

Application::Application(const ApplicationConfig& config, Validated /*validated*/)
    : mSDLHandle{std::make_unique<SDLHandle>(SDL_INIT_EVENTS)}
    , mWindow{std::make_unique<SDLWindow>(config.title.c_str(),
                                          static_cast<int>(config.size.width),
                                          static_cast<int>(config.size.height),
                                          config.hidden ? SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
                                                        : SDL_WINDOW_RESIZABLE)}
    , mStartTime{std::chrono::steady_clock::now()}
    , mScene{std::make_shared<Engine::Scene>()}
    , mRenderer{std::make_unique<Engine::Renderer>(mWindow->getSurfaceHandle(),
                                                   [&]
                                                   {
                                                       Engine::Renderer::Config renderer =
                                                           config.renderer;
                                                       if (renderer.appName.empty())
                                                       {
                                                           renderer.appName = config.title;
                                                       }
                                                       renderer.extent = mWindow->sizeInPixels();

                                                       return renderer;
                                                   }())}
{
    mRenderer->addRenderable(mScene);
}

Application::~Application()
{
    // Here, not in ~Scene: onDestroy() may use the renderer, which is alive until this body ends.
    // Without it the scene would die after the renderer.
    mScene->clear();
}

void
Application::addRenderable(std::shared_ptr<Engine::Renderable> renderable)
{
    mRenderer->addRenderable(std::move(renderable));
}

Engine::Renderer&
Application::getRenderer()
{
    return *mRenderer;
}

Engine::Scene&
Application::getScene()
{
    return *mScene;
}

void
Application::frame(Wait wait)
{
    const FlagScope inFrame{mInFrame};

    const auto now   = std::chrono::steady_clock::now();
    float      delta = 0.0f;
    if (mLastFrameTime)
    {
        delta = Engine::frameDelta(std::chrono::duration<double>{now - *mLastFrameTime}.count());
    }
    mLastFrameTime = now;
    const Engine::FrameContext frameContext{
        .timeSeconds  = std::chrono::duration<double>{now - mStartTime}.count(),
        .deltaSeconds = delta,
        .viewport     = mWindow->sizeInPixels(),
        .input        = mInput};
    // Now, so that what the events bring from here on is for the next frame: a press is in one
    // frame only, and a watch frame is a frame.
    Engine::Input::Writer{mInput}.beginFrame();

    // Here, not in onDraw(): a frame the renderer skips for want of room does not call onDraw().
    runPostedTasks();
    mScene->update(frameContext);
    if (wait == Wait::eYes)
    {
        mRenderer->submitFrame(frameContext);
    }
    else
    {
        mRenderer->trySubmitFrame(frameContext);
    }
}

void
Application::post(std::function<void()> task)
{
    if (!task)
    {
        return;
    }

    const std::scoped_lock lock{mPostMutex};
    mPosted.push_back(std::move(task));
}

void
Application::runPostedTasks()
{
    // Moved out under the lock and run without it: a task may post, and what it posts runs in the
    // next frame, so the vector that is walked is never the one that is appended to.
    std::vector<std::function<void()>> tasks;
    {
        const std::scoped_lock lock{mPostMutex};
        tasks = std::exchange(mPosted, {});
    }

    for (auto task = tasks.begin(); task != tasks.end(); ++task)
    {
        try
        {
            (*task)();
        }
        catch (...)
        {
            // The tasks behind it run first at the next run(), before what was posted since.
            const std::scoped_lock lock{mPostMutex};
            mPosted.insert(mPosted.begin(),
                           std::make_move_iterator(task + 1),
                           std::make_move_iterator(tasks.end()));
            throw;
        }
    }
}

void
Application::takeInput(const SDL_Event& event)
{
    Engine::Input::Writer writer{mInput};

    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        // A repeat of a key that is held is no new press.
        if (const auto key = keyOf(event.key.scancode); key && !event.key.repeat)
        {
            event.key.down ? writer.press(*key) : writer.release(*key);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (const auto button = mouseButtonOf(event.button.button))
        {
            event.button.down ? writer.press(*button) : writer.release(*button);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // Window coordinates are in points; the game reads pixels.
        writer.moveMouse(Math::Vector2f{event.motion.x, event.motion.y} * mWindow->pixelDensity());
        break;
    default:
        break;
    }
}

bool
Application::onEvent(void* application, SDL_Event* event)
{
    auto& self = *static_cast<Application*>(application);

    // SDL calls a watch on the thread that pushes the event. Everything here belongs to the main
    // thread.
    if (!SDL_IsMainThread())
    {
        return true;
    }

    switch (event->type)
    {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        self.mRenderer->resize(
            {.width = toPixels(event->window.data1), .height = toPixels(event->window.data2)});
        break;
    // data1 is 1 for the tick of a live resize: macOS and Windows keep the main thread in their
    // resize loop and send this event from it about 60 times a second.
    case SDL_EVENT_WINDOW_EXPOSED:
        if (event->window.data1 == 1 && !self.mInFrame && !self.mWatchFailure &&
            self.mRenderer->getState() == Engine::Renderer::State::eRunning)
        {
            try
            {
                // Never waits: the tick must not stall the resize loop.
                self.frame(Wait::eNo);
            }
            catch (...)
            {
                self.mWatchFailure = std::current_exception();
            }
        }
        break;
    default:
        break;
    }

    return true;
}

void
Application::run()
{
    mWatchFailure = nullptr;
    mLastFrameTime.reset();
    mInput = Engine::Input{};
    // Resizes before the watch exists are not seen by it.
    mRenderer->resize(mWindow->sizeInPixels());

    // A request to quit ends this run() and no other.
    struct QuitScope
    {
        Engine::Scene& scene;

        ~QuitScope() { scene.clearQuitRequest(); }
    };

    const QuitScope quitScope{*mScene};

    try
    {
        const EventWatch watch{&Application::onEvent, this};

        bool quit = false;
        while (mRenderer->getState() == Engine::Renderer::State::eRunning)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                // SDL_EVENT_QUIT is Cmd+Q, Dock "Quit" and SIGTERM/SIGINT.
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
                {
                    quit = true;
                }
                takeInput(event);
            }

            // The watch cannot throw through SDL; it left the exception here.
            if (mWatchFailure)
            {
                std::rethrow_exception(std::exchange(mWatchFailure, nullptr));
            }
            if (quit)
            {
                break;
            }

            // Waits for the render thread, which paces the loop. A render thread that failed
            // drains, so this does not hang, and getState() ends the loop.
            frame(Wait::eYes);
            if (mScene->quitRequested())
            {
                break;
            }
        }
    }
    catch (...)
    {
        if (const std::exception_ptr failure = mRenderer->failure())
        {
            logFailure(failure);
        }
        throw;
    }

    if (const std::exception_ptr failure = mRenderer->failure())
    {
        std::rethrow_exception(failure);
    }
}
} // namespace VOG::Application
