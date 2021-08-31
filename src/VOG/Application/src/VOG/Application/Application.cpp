#include "VOG/Application/Application.hpp"

#include <SFML/Window.hpp>
#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

namespace VOG::Application
{
namespace
{
std::vector<std::string>
MakeVulkanExtensionsArray(std::vector<std::string> inputExtensions)
{
    std::vector<std::string> result = std::move(inputExtensions);

    auto sfmlExtensions = sf::Vulkan::getGraphicsRequiredInstanceExtensions();
    result.reserve(result.size() + sfmlExtensions.size());
    for (const auto& elem : sfmlExtensions)
        result.emplace_back(elem);

    return result;
}
} // namespace

Application::Application(std::string  title,
                         unsigned int height,
                         unsigned int width,
                         str_list     extensions,
                         str_list     layers)
    : mWindowTitle{std::move(title)}
    , mExtensions{MakeVulkanExtensionsArray(std::move(extensions))}
    , mLayers{std::move(layers)}
    , mWindow{std::make_shared<sf::WindowBase>(
          sf::VideoMode{height, width}, sf::String{mWindowTitle}, sf::Style::Default)}
    , mRenderer{std::make_shared<Engine::Renderer>(
          Common::JSONContainer{{"extensions", mExtensions},
                                {"layers", mLayers},
                                {"app_name", std::move(mWindowTitle)},
                                {"window", mWindow->getSystemHandle()},
                                {"frames_in_flight", 2u}})}
{
}

bool
Application::Run()
{
    using namespace VOG::Common;

    if (!mRenderer->RequestRenderChangeState(VOG::Engine::Renderer::RenderJobState::Active))
    {
        return false;
    }

    while (mWindow->isOpen())
    {
        sf::Event event;
        while (mWindow->waitEvent(event))
        {
            // "close requested" event: we close the window
            if (event.type == sf::Event::Closed)
            {
                mRenderer->RequestRenderChangeState(
                    VOG::Engine::Renderer::RenderJobState::Inactive);
                mWindow->close();
            }
        }
    }

    return true;
}
} // namespace VOG::Application
