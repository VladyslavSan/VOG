#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VOG::Engine
{
class Renderable;
class Renderer;
} // namespace VOG::Engine

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
        std::uint32_t width  = 640u; // NOLINT(readability-magic-numbers)
        std::uint32_t height = 480u; // NOLINT(readability-magic-numbers)

        // Vulkan extensions
        std::vector<std::string> extensions;
        // Vulkan layers
        std::vector<std::string> layers;

        std::string shaderStoragePath;

        std::uint8_t framesInFlight = 2u;
    };

    Application(ApplicationConfig config);

    ~Application();

    const std::shared_ptr<Engine::Renderer>& renderer() const;

    /** Registers @p renderable with the renderer; call before run(). */
    void addRenderable(std::shared_ptr<Engine::Renderable> renderable);

    bool run();

protected:
    std::unique_ptr<SDLHandle>        mSDLHandle;
    std::shared_ptr<SDLWindow>        mWindow;
    std::shared_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
