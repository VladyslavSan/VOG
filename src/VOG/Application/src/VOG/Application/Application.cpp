#include "VOG/Application/Application.hpp"

#include <VOG/Application/SDL.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

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
/** How often run() looks at the render thread when no event arrives. */
constexpr Sint32 gEventTimeoutMs = 100;

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
                                          SDL_WINDOW_RESIZABLE)}
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

void
Application::run()
{
    mRenderer->start();

    // Wakes every gEventTimeoutMs, so a render thread that failed is noticed without an event.
    bool quit = false;
    while (!quit && mRenderer->getState() == Engine::Renderer::State::eRunning)
    {
        SDL_Event event;
        if (!SDL_WaitEventTimeout(&event, gEventTimeoutMs))
        {
            continue;
        }

        switch (event.type)
        {
        // SDL_EVENT_QUIT is Cmd+Q, Dock "Quit" and SIGTERM/SIGINT.
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            quit = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            mRenderer->resize(
                {.width = toPixels(event.window.data1), .height = toPixels(event.window.data2)});
            break;
        default:
            break;
        }
    }

    if (const std::exception_ptr failure = mRenderer->stop())
    {
        std::rethrow_exception(failure);
    }
}
} // namespace VOG::Application
