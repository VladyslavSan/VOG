#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

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
class Queue;
class Swapchain;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
/**
 * Owns the Vulkan objects of one window and the thread that renders into it.
 *
 * The render thread owns nothing: it captures `this`, is the last member and is joined before
 * anything it uses is destroyed. Nothing reachable from it holds a pointer to the Renderer, so
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
        vk::Extent2D extent;
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
     * Queues @p renderable; its prepare() runs on the render thread before its first collect().
     * Any thread, including a Renderable callback: no callback runs under a lock, and a
     * renderable queued while another one prepares is prepared in the same frame.
     */
    void addRenderable(std::shared_ptr<Renderable> renderable);

    /**
     * New window size in pixels. Any thread; the render thread recreates the swapchain at that
     * size before its next frame. Surfaces that report their own size (macOS, Windows, X11) ignore
     * it; Wayland and headless surfaces take it as given.
     */
    void resize(vk::Extent2D extent);

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
    /** min(framesInFlight, imageCount); logs when it clamps. */
    static std::uint8_t frameSlotCount(std::uint8_t framesInFlight, std::size_t imageCount);

    /** Locks mPendingMutex only. */
    std::shared_ptr<Renderable> takePendingRenderable();

    /** Render thread. Drains the pending queue one renderable at a time; no lock is held while a
     *  renderable prepares, and one whose prepare() throws is dropped. */
    void prepareRenderables();

    /** Render thread. Refills mRenderItems and checks every item. */
    void collectRenderItems(const FrameContext& frameContext);

    /** Render thread. Rebuilds the swapchain for @p requested; false if it is zero-sized. */
    bool applyExtent(vk::Extent2D requested);

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

    /** Written by resize() on any thread; vk::Extent2D is trivially copyable, so this is lock-free.
     */
    std::atomic<vk::Extent2D> mRequestedExtent;

    /** Render thread only: the last requested extent that was applied. Surfaces that report their
     *  own size ignore the request, so this is not necessarily the size of the images. */
    vk::Extent2D mAppliedExtent;

    /** Written by any thread, drained by the render thread. No callback runs under it. */
    std::mutex                              mPendingMutex;
    std::deque<std::shared_ptr<Renderable>> mPending;

    /** Render thread only; everything in it has been prepared. */
    std::vector<std::shared_ptr<Renderable>> mRenderables;

    /** Render-thread scratch space, kept to reuse its capacity across frames. */
    std::vector<RenderItem> mRenderItems;

    /** Serializes start, stop and getState. */
    mutable std::mutex mStateMutex;

    /** Fulfilled by the render thread when it exits; get() rethrows its failure. */
    std::future<void> mExit;

    /** Last member: joined before anything above is destroyed. */
    std::jthread mRenderThread;
};
} // namespace VOG::Engine
