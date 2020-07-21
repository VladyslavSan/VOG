#include <SFML/Window.hpp>
#include <VOG/Application/Application.hpp>
#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Common/Signals.hpp>
#include <VOG/Engine/Renderer.hpp>

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

Application::Application(std::string title, unsigned int height, unsigned int width,
                         str_list extensions, str_list layers)
    : m_windowTitle{std::move(title)}
    , m_extensions{MakeVulkanExtensionsArray(std::move(extensions))}
    , m_layers{std::move(layers)}
    , m_window{std::make_shared<sf::WindowBase>(sf::VideoMode{height, width},
                                                sf::String{m_windowTitle}, sf::Style::Default)}
    , m_renderer{std::make_shared<Engine::Renderer>(
          Common::JSONContainer{{"extensions", m_extensions},
                                {"layers", m_layers},
                                {"app_name", std::move(m_windowTitle)},
                                {"window", m_window->getSystemHandle()},
                                {"frames_in_flight", 2}})}
{
}

bool
Application::Run()
{
    using namespace VOG::Common;

    const bool Result =
        m_renderer->RequestRenderChangeState(VOG::Engine::Renderer::RenderJobState::Active);

    if (Result)
    {
        auto flag = std::make_shared<std::atomic_bool>();
        flag->store(false);
        Signal::Get().RegisterSignal(
            SignalType::Interrupt, [weakRenderer = std::weak_ptr<Engine::Renderer>(m_renderer),
                                    flag = std::weak_ptr{flag}]() {
                auto renderer = weakRenderer.lock();
                if (renderer)
                    renderer->RequestRenderChangeState(Engine::Renderer::RenderJobState::Inactive);

                auto flag_locked = flag.lock();
                if (flag_locked)
                    flag_locked->store(true);
            });

        while (!flag->load() && m_window->isOpen())
        {
            sf::Event event;
            while (m_window->pollEvent(event))
            {
                // "close requested" event: we close the window
                if (event.type == sf::Event::Closed)
                    m_window->close();
            }
        }
    }

    return Result;
}
} // namespace VOG::Application
