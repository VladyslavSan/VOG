#include "VOG/Application/Application.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Application/SDL2.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <SDL2/SDL_vulkan.h>
#include <boost/process.hpp>
#include <boost/program_options.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <optional>

namespace po = boost::program_options;

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
{

    auto env = boost::process::environment::current();

    std::string             shaderStoragePath = {};
    po::options_description description       = {};
    description.add_options()("shader-storage-path",
                              po::value(&shaderStoragePath),
                              "Path to the shader directory containing config.json.");
    const auto        parsed = po::parse_command_line(cmdArgs.size(), cmdArgs.data(), description);
    po::variables_map vm{};
    po::store(parsed, vm);
    po::notify(vm);

    if (shaderStoragePath.empty())
    {
        throw std::runtime_error("Shader storage path is empty.");
    }
    else if (!std::filesystem::exists(shaderStoragePath))
    {
        throw std::runtime_error(
            fmt::format("Shader storage path \"{}\" does not exist.", shaderStoragePath));
    }

    mRenderer = {std::make_shared<Engine::Renderer>(
        mWindow->getSurfaceHandle(),
        Common::JSONContainer{{"extensions", extensions},
                              {"layers", layers},
                              {"app_name", title},
                              {"frames_in_flight", 2u},
                              {"shader_source_path", shaderStoragePath}})};
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
