#include "VOG/Application/Application.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <SDL2/SDL_vulkan.h>
#include <VOG/Application/SDL2.hpp>
#include <optional>

namespace VOG::Application
{

Application::Application(const std::string& title,
                         unsigned int       width,
                         unsigned int       height,
                         const StringList&  extensions,
                         const StringList&  layers,
                         CmdArgs            cmdArgs)
    : mSDLHandle{std::make_unique<SDLHandle>(SDL_INIT_EVENTS)}
    , mWindow{std::make_shared<SDLWindow>(title.c_str(),
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          static_cast<int>(width),
                                          static_cast<int>(height),
                                          SDL_WINDOW_SHOWN)}
    , mRenderer{std::make_shared<Engine::Renderer>(
          Common::JSONContainer{{"extensions", extensions},
                                {"layers", layers},
                                {"app_name", title},
                                {"surface", mWindow->getSurfaceHandles()},
                                {"frames_in_flight", 2u}})}
{
}

Application::~Application() {}

bool
Application::Run()
{
    using namespace VOG::Common;

    if (!mRenderer->RequestRenderChangeState(VOG::Engine::Renderer::RenderJobState::Active))
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
            mRenderer->RequestRenderChangeState(VOG::Engine::Renderer::RenderJobState::Inactive);
            mRenderer.reset();
            break;
        }
    }

    return true;
}
} // namespace VOG::Application
