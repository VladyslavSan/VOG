#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;

namespace VOG::Engine
{
class Renderable;
class Renderer;
class Scene;
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
        /** Test seam, like RendererTestAccess: the window is created hidden, so a test does not
         *  flash one. It still has a surface and presents. No production code sets it. */
        bool hidden = false;

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

    /** For createMesh and createMaterial; run() owns start and stop. */
    Engine::Renderer& getRenderer();

    /**
     * The scene run() updates once per frame and draws. Main thread, outside run() or in the
     * Behaviours it updates. The destructor clears it, so every Behaviour::onDestroy() runs while
     * the renderer still exists.
     */
    Engine::Scene& getScene();

    /**
     * Starts rendering and, on the calling thread, pumps events and runs a frame per iteration
     * (Scene::update(), then the submission of the draws), until a quit is requested or the
     * render thread fails; then stops the renderer and rethrows that failure. May be called
     * again. An exception from a Renderable::onDraw() or from a Behaviour::start() or update()
     * stops the renderer too and reaches the caller.
     *
     * Call it on the main thread. While the OS resizes the window, macOS and Windows keep the
     * calling thread in their own loop instead of returning to run(); an SDL event watch runs
     * frames from there, so the window keeps drawing (Behaviour::update() and
     * Renderable::onDraw() run inside it).
     */
    void run();

private:
    struct Validated
    {
    };

    enum class Wait : std::uint8_t
    {
        /** Submit the frame once the render thread has room for it. */
        eYes,
        /** Skip the frame if the render thread has none. */
        eNo
    };

    Application(const ApplicationConfig& config, Validated);

    /** SDL event watch; SDL calls it on the thread that pushes an event. */
    static bool onEvent(void* application, SDL_Event* event);

    /** Main thread. Updates the scene, then submits one frame to the renderer. */
    void frame(Wait wait);

    std::unique_ptr<SDLHandle> mSDLHandle;
    std::unique_ptr<SDLWindow> mWindow;

    // The next four are main-thread state: run() and the event watch use them.
    std::chrono::steady_clock::time_point mStartTime;
    /** The previous frame of this run(), for FrameContext::deltaSeconds. */
    std::optional<std::chrono::steady_clock::time_point> mLastFrameTime;
    /** True while frame() runs, so a Renderable that pumps events cannot start a nested one. */
    bool mInFrame = false;
    /** Set by a watch tick that threw; run() rethrows it, because SDL cannot unwind. */
    std::exception_ptr mWatchFailure;

    /** Also in the renderables of mRenderer, which is destroyed first. */
    std::shared_ptr<Engine::Scene> mScene;

    /** Last member: destroyed first, which joins the render thread and then tears the GPU down. */
    std::unique_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
