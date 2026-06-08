#pragma once

#include <memory>
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
    struct ApplicationConfig
    {
        // Generic application info.
        std::string   title  = "Application";
        std::uint32_t width  = 640;
        std::uint32_t height = 480;

        // Vulkan extensions
        std::vector<std::string> extensions;
        // Vulkan layers
        std::vector<std::string> layers;

        std::string shaderStoragePath;
    };

    Application(ApplicationConfig config);

    ~Application();

    bool run();

protected:
    std::unique_ptr<SDLHandle>        mSDLHandle;
    std::shared_ptr<SDLWindow>        mWindow;
    std::shared_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
