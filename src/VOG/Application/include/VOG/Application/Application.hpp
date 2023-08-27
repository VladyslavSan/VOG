#pragma once

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace VOG::Engine
{
class Renderer;
}

namespace VOG::Application
{
struct SDLHandle;
class SDLWindow;
class Application
{
public:
    using StringList = std::vector<std::string>;
    using CmdArgs    = std::span<const char*>;

    Application(const std::string& title,
                unsigned int       width,
                unsigned int       height,
                const StringList&  extensions = {},
                const StringList&  layers     = {},
                CmdArgs            cmdArgs    = {});

    ~Application();

    bool run();

protected:
    std::unique_ptr<SDLHandle>        mSDLHandle;
    std::shared_ptr<SDLWindow>        mWindow;
    std::shared_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
