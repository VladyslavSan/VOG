#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace sf
{
class WindowBase;
}

namespace VOG::Engine
{
class Renderer;
}

namespace VOG::Application
{

class Application
{
public:
    typedef std::vector<std::string> str_list;

    Application(std::string title, unsigned int height, unsigned int width,
                str_list extensions = {}, str_list layers = {});

    bool Run();

protected:
    std::string m_windowTitle;
    str_list m_extensions;
    str_list m_layers;

    std::shared_ptr<sf::WindowBase> m_window;
    std::shared_ptr<Engine::Renderer> m_renderer;
};
} // namespace VOG::Application
