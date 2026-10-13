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

class Application final
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

    /** Validates @p config before SDL is initialized or a window exists. */
    explicit Application(const ApplicationConfig& config);

    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    /** Any time, before or between run()s. */
    void addRenderable(std::shared_ptr<Engine::Renderable> renderable);

    /**
     * Starts rendering and pumps events on the calling thread until a quit is requested or the
     * render thread fails; then stops the renderer and rethrows that failure. May be called again.
     */
    void run();

private:
    struct Validated
    {
    };

    Application(const ApplicationConfig& config, Validated);

    std::unique_ptr<SDLHandle> mSDLHandle;
    std::unique_ptr<SDLWindow> mWindow;
    /** Last member: destroyed first, which joins the render thread and then tears the GPU down. */
    std::unique_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
