#include "VOG/Application/Application.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Application/SDL2.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <SDL2/SDL_vulkan.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <format>

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
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          static_cast<int>(config.width),
                                          static_cast<int>(config.height),
                                          SDL_WINDOW_SHOWN)}
{
    validateConfig(config);

    mRenderer = {std::make_shared<Engine::Renderer>(
        mWindow->getSurfaceHandle(),
        Common::JSONContainer{{"extensions", config.extensions},
                              {"layers", config.layers},
                              {"app_name", config.title},
                              {"frames_in_flight", 2u},
                              {"shader_source_path", config.shaderStoragePath}})};
}

Application::~Application() {}

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
        const bool waitEventSuccess = SDL_WaitEventTimeout(&event, 100) == SDL_TRUE;
        if (waitEventSuccess && event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE)
        {
            mRenderer->requestRenderChangeState(VOG::Engine::Renderer::RenderJobState::eInactive);
            mRenderer.reset();
            break;
        }
    }

    return true;
}
} // namespace VOG::Application
