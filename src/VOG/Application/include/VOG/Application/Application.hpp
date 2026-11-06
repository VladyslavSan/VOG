#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Input.hpp>
#include <VOG/Engine/Renderer/Renderer.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

union SDL_Event;

namespace VOG::Engine
{
class Renderable;
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
        std::string title = "Application";
        /** The size of the window, in the units of the window system (points, where a pixel is
         *  smaller). */
        Engine::Extent size{.width = 640u, .height = 480u}; // NOLINT(readability-magic-numbers)
        /** Test seam, like RendererTestAccess: the window is created hidden, so a test does not
         *  flash one. It still has a surface and presents. No production code sets it. */
        bool hidden = false;

        /** The renderer of the window. Its appName is the title if it is empty here (a name set
         *  here is kept). Its extent is always the size of the window in pixels: an extent set
         *  here is replaced. */
        Engine::Renderer::Config renderer;
    };

    /** Validates @p config before SDL is initialized or a window exists.
     *  @throws std::runtime_error if the shader path of the renderer is empty or does not exist. */
    explicit Application(const ApplicationConfig& config);

    ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    /** Any time, before or between run()s. */
    void addRenderable(std::shared_ptr<Engine::Renderable> renderable);

    /** For createMesh and createMaterial. The render thread runs from the construction of the
     *  renderer, so a resource made here is copied while run() is not called too. */
    Engine::Renderer& getRenderer();

    /**
     * The scene run() updates once per frame and draws. Main thread, outside run() or in the
     * Behaviours it updates. The destructor clears it, so every Behaviour::onDestroy() runs while
     * the renderer still exists.
     */
    Engine::Scene& getScene();

    /**
     * Runs @p task on the main thread at the start of a frame, before Scene::update(). Any thread,
     * a posted task included: the task that a task posts runs in the frame after. It is the way a
     * background loader hands a finished resource to the Scene. A task that throws ends run()
     * like a Behaviour that throws, and the tasks behind it run first at the next run(). A task
     * that is posted while no run() runs waits for the next one. An empty function is ignored.
     */
    void post(std::function<void()> task);

    /**
     * On the calling thread, pumps events, fills the input of the frame, and runs a frame per
     * iteration (the posted tasks, Scene::update(), then the submission of the draws), until the
     * window is closed, Scene::requestQuit() was called (run() returns after the frame that
     * called it, and forgets the request), or the render thread fails; then rethrows that failure.
     * May be called again, unless the render thread failed: that is final, and every call
     * rethrows the failure at once. An exception from a posted task, a Renderable::onDraw(), a
     * DrawError of a draw included, or from a Behaviour::start() or update() ends run() and
     * reaches the caller; the renderer keeps running.
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

    /** Main thread. Copies the input into the context of the frame and starts the next one's, so
     *  that a press is in one frame only, a watch frame included. Runs the posted tasks, updates
     *  the scene, then submits one frame to the renderer. */
    void frame(Wait wait);

    /** Main thread. Runs the tasks posted until now; the tasks behind one that throws stay. */
    void runPostedTasks();

    /** Main thread. Puts the event into the input of the next frame, if it is a key or a mouse
     *  event. */
    void takeInput(const SDL_Event& event);

    std::unique_ptr<SDLHandle> mSDLHandle;
    std::unique_ptr<SDLWindow> mWindow;

    // The next five are main-thread state: run() and the event watch use them.
    std::chrono::steady_clock::time_point mStartTime;
    /** The previous frame of this run(), for FrameContext::deltaSeconds. */
    std::optional<std::chrono::steady_clock::time_point> mLastFrameTime;
    /** True while frame() runs, so a Renderable that pumps events cannot start a nested one. */
    bool mInFrame = false;
    /** Set by a watch tick that threw; run() rethrows it, because SDL cannot unwind. */
    std::exception_ptr mWatchFailure;
    /** What the keyboard and the mouse did since the last frame; each frame gets a copy and then
     *  starts the next frame's input. */
    Engine::Input mInput;

    /** Written by any thread (post()), drained by the main thread. No task runs under it. */
    std::mutex                         mPostMutex;
    std::vector<std::function<void()>> mPosted;

    /** Also in the renderables of mRenderer, which is destroyed first. */
    std::shared_ptr<Engine::Scene> mScene;

    /** Last member: destroyed first, which joins the render thread and then tears the GPU down. */
    std::unique_ptr<Engine::Renderer> mRenderer;
};
} // namespace VOG::Application
