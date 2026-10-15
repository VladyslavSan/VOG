#include "VOG/Application/Application.hpp"

#include <VOG/Application/SDL.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

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
    if (config.shaderStoragePath.empty())
    {
        throw std::runtime_error("Shader storage path is empty.");
    }
    if (!std::filesystem::exists(config.shaderStoragePath))
    {
        throw std::runtime_error(
            std::format("Shader storage path \"{}\" does not exist.", config.shaderStoragePath));
    }
}

/** SDL reports sizes as signed integers. */
std::uint32_t
toPixels(Sint32 size)
{
    return static_cast<std::uint32_t>(std::max(size, Sint32{0}));
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
                                          static_cast<int>(config.width),
                                          static_cast<int>(config.height),
                                          config.hidden ? SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
                                                        : SDL_WINDOW_RESIZABLE)}
    , mStartTime{std::chrono::steady_clock::now()}
    , mRenderer{std::make_unique<Engine::Renderer>(mWindow->getSurfaceHandle(),
                                                   Engine::Renderer::Config{
                                                       .appName          = config.title,
                                                       .engineName       = "VOG",
                                                       .layers           = config.layers,
                                                       .extensions       = config.extensions,
                                                       .framesInFlight   = config.framesInFlight,
                                                       .shaderSourcePath = config.shaderStoragePath,
                                                       .extent           = mWindow->sizeInPixels(),
                                                   })}
{
}

Application::~Application() = default;

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

void
Application::frame(Wait wait)
{
    const FlagScope inFrame{mInFrame};

    const Engine::FrameContext frameContext{
        .timeSeconds =
            std::chrono::duration<double>{std::chrono::steady_clock::now() - mStartTime}.count()};
    if (wait == Wait::eYes)
    {
        mRenderer->submitFrame(frameContext);
    }
    else
    {
        mRenderer->trySubmitFrame(frameContext);
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
    // Resizes before the watch exists are not seen by it.
    mRenderer->resize(mWindow->sizeInPixels());
    mRenderer->start();

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
        }
    }
    catch (...)
    {
        if (const std::exception_ptr failure = mRenderer->stop())
        {
            logFailure(failure);
        }
        throw;
    }

    if (const std::exception_ptr failure = mRenderer->stop())
    {
        std::rethrow_exception(failure);
    }
}
} // namespace VOG::Application
