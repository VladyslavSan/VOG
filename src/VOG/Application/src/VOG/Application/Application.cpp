#include "VOG/Application/Application.hpp"

#include <VOG/Application/SDL.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <spdlog/spdlog.h>

#include <filesystem>
#include <format>
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
} // namespace

Application::Application(ApplicationConfig config)
    : mSDLHandle{std::make_unique<SDLHandle>(SDL_INIT_EVENTS)}
    , mWindow{std::make_shared<SDLWindow>(config.title.c_str(),
                                          static_cast<int>(config.width),
                                          static_cast<int>(config.height),
                                          SDL_WINDOW_RESIZABLE)}
{
    validateConfig(config);

    mRenderer = std::make_shared<Engine::Renderer>(mWindow->getSurfaceHandle(),
                                                   Engine::Renderer::Config{
                                                       .appName          = config.title,
                                                       .engineName       = "VOG",
                                                       .layers           = config.layers,
                                                       .extensions       = config.extensions,
                                                       .framesInFlight   = config.framesInFlight,
                                                       .shaderSourcePath = config.shaderStoragePath,
                                                   });
}

Application::~Application() {}

const std::shared_ptr<Engine::Renderer>&
Application::renderer() const
{
    return mRenderer;
}

void
Application::addRenderable(std::shared_ptr<Engine::Renderable> renderable)
{
    mRenderer->addRenderable(std::move(renderable));
}

bool
Application::run()
{
    using namespace VOG::Common;

    if (!mRenderer->requestRenderChangeState(VOG::Engine::Renderer::RenderJobState::eActive))
    {
        return false;
    }

    while (true)
    {
        SDL_Event  event;
        const bool waitEventSuccess = SDL_WaitEventTimeout(&event, 100);
        if (waitEventSuccess && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            mRenderer->requestRenderChangeState(VOG::Engine::Renderer::RenderJobState::eInactive);
            mRenderer.reset();
            break;
        }
    }

    return true;
}
} // namespace VOG::Application
