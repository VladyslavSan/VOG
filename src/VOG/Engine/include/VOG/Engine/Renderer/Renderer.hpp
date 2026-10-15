#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Camera.hpp>
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace VOG::Graphics
{
class ShaderProgramCache;
} // namespace VOG::Graphics

namespace VOG::Graphics::Frame
{
class FrameObjectManager;
} // namespace VOG::Graphics::Frame

namespace VOG::Graphics::Vulkan
{
class Instance;
class Device;
class FencePool;
class GraphicsPipeline;
class Queue;
class Swapchain;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class PipelineCache;

/**
 * Owns the Vulkan objects of one window and the thread that renders into it.
 *
 * The render thread owns nothing: it captures `this`, is the last member and is joined before
 * anything it uses is destroyed. Nothing reachable from it owns the Renderer, so
 * `~Renderer` cannot run on the render thread. Calling stop() or getState() from the render
 * thread (a Renderable that captured `Renderer&`) is not supported: stop() would join itself and
 * terminate, and getState() would wait for the lock that stop() holds across the join, a
 * deadlock.
 */
class Renderer final
{
public:
    static constexpr std::uint8_t kMaxFramesInFlight = 3;

    /** Everything the renderer needs to bring up Vulkan; JSON stays at the file-loading edge. */
    struct Config
    {
        std::string appName;
        std::string engineName;

        std::vector<std::string> layers;
        std::vector<std::string> extensions;

        std::uint8_t framesInFlight = 1u;

        std::filesystem::path shaderSourcePath;

        /**
         * Size of the window in pixels at startup. It sizes the swapchain where the surface
         * leaves that to the application (Wayland, headless): required there, 0x0 throws.
         */
        Extent extent;
    };

    enum class State : std::uint8_t
    {
        /** No render thread: not started, or stopped. */
        eStopped,
        eRunning,
        /** The render thread ended with an exception; stop() returns it. */
        eFailed
    };

    Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config);

    /** Stops the render thread. A failure nobody collected with stop() is logged. */
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    /**
     * Queues @p renderable; the render thread collects from it from the next frame on. Any
     * thread, including a Renderable callback: no callback runs under a lock.
     */
    void addRenderable(std::shared_ptr<Renderable> renderable);

    /**
     * Uploads the vertices of @p description. Any thread: the allocator is internally
     * synchronized.
     * @throws std::invalid_argument if the description is empty, its size is not a multiple of the
     *         stride, an attribute overruns the stride, or it has more attributes than a pipeline
     *         can take.
     */
    MeshPtr createMesh(const MeshDescription& description);

    /**
     * Looks up the shader of @p description. Any thread: the shader cache is only read.
     * @throws std::invalid_argument for an unknown shader, more than 64 bytes of parameters, or a
     *         push-constant range of the shader that ends past the parameters.
     */
    MaterialPtr createMaterial(MaterialDescription description);

    /** Any thread. A default Camera is used until this is called. */
    void setCamera(const Camera& camera);

    /**
     * New window size in pixels. Any thread; the render thread recreates the swapchain at that
     * size before its next frame. Surfaces that report their own size (macOS, Windows, X11) ignore
     * it; Wayland and headless surfaces take it as given.
     */
    void resize(Extent extent);

    /** Starts the render thread. @throws std::logic_error unless getState() is eStopped. */
    void start();

    /**
     * Requests stop, joins the render thread and returns its failure, or null if it ended
     * normally or was not running. The Renderer can be started again afterwards. The stop is
     * observed within one acquire timeout and one frame.
     */
    [[nodiscard]] std::exception_ptr stop() noexcept;

    State getState() const;

private:
    /** Lets RendererTest read the swapchain; nothing else uses it. */
    friend struct RendererTestAccess;

    /** min(framesInFlight, imageCount); logs when it clamps. */
    static std::uint8_t frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount);

    /** Render thread. Moves the pending renderables into mRenderables. */
    void adoptPendingRenderables();

    /** Render thread. Refills mDraws, checks every draw and finds its pipeline in mPipelines. */
    void collectDraws(const FrameContext& frameContext);

    /** Render thread. Rebuilds the swapchain for @p requested; false if it is zero-sized. */
    bool applyExtent(Extent requested);

    /** Render thread. One frame; an exception ends the thread. */
    void renderFrame();

    const std::uint8_t mFramesInFlight;

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<const Graphics::Vulkan::Queue>       mGraphicsQueue;
    std::shared_ptr<Graphics::Vulkan::FencePool>         mFencePool;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::unique_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::unique_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    std::chrono::steady_clock::time_point mStartTime;

    /** Written by resize() on any thread. Extent is trivially copyable and 8 bytes, so the atomic
     * is lock-free (Renderer.cpp asserts it). */
    std::atomic<Extent> mRequestedExtent;

    /** Render thread only: the last requested extent that was applied. Surfaces that report their
     *  own size ignore the request, so this is not necessarily the size of the images. */
    Extent mAppliedExtent;

    /** Written by any thread, drained by the render thread. No callback runs under it. */
    std::mutex                              mPendingMutex;
    std::deque<std::shared_ptr<Renderable>> mPending;

    /** Render thread only. */
    std::vector<std::shared_ptr<Renderable>> mRenderables;

    /** A mutex, not an atomic: Camera is too big to be lock-free without libatomic. */
    std::mutex mCameraMutex;
    Camera     mCamera;

    /** Render thread only. */
    std::unique_ptr<PipelineCache> mPipelineCache;

    /** Render-thread scratch space, kept to reuse its capacity across frames. The draws, and so
     *  the meshes, stay alive until the next frame's collect clears them. */
    DrawList mDraws;
    /** The pipeline of each draw in mDraws, at the same index. */
    std::vector<std::shared_ptr<Graphics::Vulkan::GraphicsPipeline>> mPipelines;

    /** Serializes start, stop and getState. */
    mutable std::mutex mStateMutex;

    /** Fulfilled by the render thread when it exits; get() rethrows its failure. */
    std::future<void> mExit;

    /** Last member: joined before anything above is destroyed. */
    std::jthread mRenderThread;
};
} // namespace VOG::Engine
