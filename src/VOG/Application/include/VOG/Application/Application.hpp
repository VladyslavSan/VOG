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

    Application(std::string  title,
                unsigned int height,
                unsigned int width,
                str_list     extensions = {},
                str_list     layers     = {});

    bool Run();

protected:
    std::string mWindowTitle;
    str_list    mExtensions;
    str_list    mLayers;

    std::shared_ptr<sf::WindowBase>   mWindow;
    std::shared_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
