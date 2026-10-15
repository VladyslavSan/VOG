#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>

#include <atomic>
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
class FramePacketQueue;
class PipelineCache;
struct FramePacket;

/**
 * Owns the Vulkan objects of one window and the thread that renders into it.
 *
 * Two threads work with it. The main thread, or whichever thread calls submitFrame(), runs the
 * Renderables: it calls onDraw() for each frame and queues the result as a packet. The render
 * thread takes the packets and records and presents them; it runs no user code.
 *
 * The render thread owns nothing: it captures `this`, is the last member and is joined before
 * anything it uses is destroyed. Nothing reachable from it owns the Renderer, so
 * `~Renderer` cannot run on the render thread.
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
     * Queues @p renderable; the next submitFrame() adopts it. Any thread, including a Renderable
     * callback: no callback runs under a lock.
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

    /**
     * New window size in pixels. Any thread; the render thread recreates the swapchain at that
     * size before its next frame. Surfaces that report their own size (macOS, Windows, X11) ignore
     * it; Wayland and headless surfaces take it as given.
     */
    void resize(Extent extent);

    /** Starts the render thread. @throws std::logic_error unless getState() is eStopped. */
    void start();

    /**
     * Calls Renderable::onDraw() of every renderable on the calling thread and queues the draws
     * with the camera view an onDraw() set as one frame. Waits while the render thread still holds
     * the previous frame, so this paces the caller to the render thread. Does nothing when the
     * renderer is stopped. When the render thread has failed it still calls onDraw(), and the frame
     * is dropped; check getState().
     *
     * One thread at a time, and not from onDraw(). An exception from onDraw() reaches the caller;
     * the renderer keeps running. stop() from another thread wakes a call that waits.
     */
    void submitFrame(const FrameContext& frameContext);

    /**
     * Like submitFrame(), but never waits for the render thread: if it still holds the previous
     * frame, returns at once without calling onDraw(). It may wait for a start() or stop() in
     * progress on another thread. For callers that must not block on the render thread, like an
     * event watch.
     */
    void trySubmitFrame(const FrameContext& frameContext);

    /**
     * Requests stop, joins the render thread and returns its failure, or null if it ended
     * normally or was not running. The Renderer can be started again afterwards. The stop is
     * observed within one acquire timeout and one frame. A packet still queued at stop() is
     * rendered first after the next start().
     */
    [[nodiscard]] std::exception_ptr stop() noexcept;

    State getState() const;

private:
    /** Lets RendererTest read the swapchain, the packet queue and the pipeline cache; nothing else
     *  uses it. */
    friend struct RendererTestAccess;

    /** min(framesInFlight, imageCount); logs when it clamps. */
    static std::uint8_t frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount);

    /** Submitting thread. Moves the pending renderables into mRenderables. */
    void adoptPendingRenderables();

    /** Submitting thread. Calls onDraw() of every renderable and returns the draws and the
     *  camera view they set as a packet. */
    FramePacket makePacket(const FrameContext& frameContext);

    /** Render thread. Finds the pipeline of each draw of @p draws in mPipelines. */
    void resolvePipelines(const RenderQueue& draws);

    /** Render thread. Rebuilds the swapchain for @p requested; false if it is zero-sized. */
    bool applyExtent(Extent requested);

    /** Render thread. One frame; an exception ends the thread. */
    void renderFrame(FramePacket packet);

    const std::uint8_t mFramesInFlight;

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<const Graphics::Vulkan::Queue>       mGraphicsQueue;
    std::shared_ptr<Graphics::Vulkan::FencePool>         mFencePool;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::unique_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::unique_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    /** Written by resize() on any thread. Extent is trivially copyable and 8 bytes, so the atomic
     * is lock-free (Renderer.cpp asserts it). */
    std::atomic<Extent> mRequestedExtent;

    /** Render thread only: the last requested extent that was applied. Surfaces that report their
     *  own size ignore the request, so this is not necessarily the size of the images. */
    Extent mAppliedExtent;

    /** Written by any thread, drained by the submitting thread. No callback runs under it. */
    std::mutex                              mPendingMutex;
    std::deque<std::shared_ptr<Renderable>> mPending;

    /** Submitting thread only. */
    std::vector<std::shared_ptr<Renderable>> mRenderables;

    /** Render thread only. */
    std::unique_ptr<PipelineCache> mPipelineCache;

    /** The pipeline of each draw of the frame being rendered, at the same index. Render-thread
     *  scratch space, kept to reuse its capacity across frames. */
    std::vector<std::shared_ptr<Graphics::Vulkan::GraphicsPipeline>> mPipelines;

    /** The frames between the submitting thread and the render thread. Declared above
     *  mRenderThread, which is joined before it is destroyed. */
    std::unique_ptr<FramePacketQueue> mPackets;

    /** Serializes start, stop and getState. */
    mutable std::mutex mStateMutex;

    /** Fulfilled by the render thread when it exits; get() rethrows its failure. */
    std::future<void> mExit;

    /** Last member: joined before anything above is destroyed. */
    std::jthread mRenderThread;
};
} // namespace VOG::Engine
