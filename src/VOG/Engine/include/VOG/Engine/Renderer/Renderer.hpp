#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Renderer/Texture.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
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
class Buffer;
class Device;
class DescriptorHeap;
class FencePool;
class GraphicsPipeline;
class Queue;
class Swapchain;
class Texture;
class TimelineSemaphore;
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

        /**
         * Optional. Called with every Vulkan message of ERROR severity (validation layer, loader,
         * driver), from the construction of the Renderer on, so it also sees errors raised while
         * the instance, the device and the swapchain are created. The Renderer turns on
         * VK_EXT_debug_utils for it, and the constructor throws std::runtime_error if the
         * instance does not offer it. Without it the Renderer installs no messenger.
         *
         * Any thread that makes a Vulkan call may call it, at the same time as another. For
         * example: the constructing thread, the render thread, the callers of createMesh(),
         * createTexture() and createMaterial(), the thread that runs ~Renderer, and any thread
         * that drops the last handle of a mesh, texture or material. So it must be thread-safe,
         * must not call the Renderer, and must not throw (an exception is logged and dropped).
         * The view is valid only during the call. A copy lives as long as anything the Renderer
         * made (meshes, textures, materials), which can be after ~Renderer: whatever it captures
         * must live that long.
         */
        std::function<void(std::string_view)> onValidationMessage;
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
     * Copies the vertices and indices of @p description into staging memory. The render thread
     * copies them into the memory of the mesh, preferably device-local, on the transfer queue, in
     * the first frame it renders after this call, and a draw of the mesh in that frame waits for
     * the copy on the GPU. The upload stays pending through frames that are dropped or fail. Any
     * thread: the allocator is internally synchronized.
     * @throws std::invalid_argument if the description is empty, the stride is zero, or its size is
     *         not a multiple of the stride; an attribute overruns the stride; it has more
     *         attributes than a pipeline can take; the stride or an attribute offset is not a
     *         multiple of 4, or exceeds maxVertexInputBindingStride or
     *         maxVertexInputAttributeOffset of the device; its index count is not a multiple of 3;
     *         or an index is not less than the vertex count.
     */
    MeshPtr createMesh(const MeshDescription& description);

    /**
     * Copies the pixels of @p description. Any thread: the allocator is internally synchronized.
     * The image is uploaded by the render thread on the transfer queue, in the first frame it
     * renders after this call, and a draw of the texture in that frame waits on the GPU for the
     * upload's timeline signal. The upload stays pending through frames that are dropped or fail.
     * @throws std::invalid_argument if the extent is zero or exceeds maxImageDimension2D of the
     *         device, or the pixels are not exactly width * height * 4 bytes.
     * @throws std::runtime_error if the renderer's descriptor heap is full or the image cannot be
     *         created.
     */
    TexturePtr createTexture(const TextureDescription& description);

    /**
     * Looks up the shader of @p description and copies the parameters into a uniform block. Any
     * thread: the shader cache is only read, and the allocator is internally synchronized.
     * @throws std::invalid_argument for an unknown shader, more than 16384 bytes of parameters,
     *         more than 4 textures, a null texture, or a push-constant range of the shader that
     *         ends past the 84 bytes the renderer pushes.
     * @throws std::runtime_error if the renderer's descriptor heap is full.
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
     * normally or was not running. The Renderer can be started again afterwards. After a failure
     * between the swapchain acquire and the enqueue of the present (a present that enqueues
     * nothing is one), a restart may stall until the swapchain is recreated, or misuse a semaphore
     * (known gap in docs/SwapchainSync.md). The stop is observed within one acquire timeout and one
     * frame, plus the fence waits of the frame slot, which have no timeout (docs/SwapchainSync.md).
     * After a normal stop, a packet still queued at stop() is rendered first after the next
     * start(); after a failure, the packets queued before stop() are dropped.
     */
    [[nodiscard]] std::exception_ptr stop() noexcept;

    State getState() const;

private:
    /** Lets RendererTest read the swapchain, the packet queue, the pipeline cache and the pending
     *  uploads; nothing else uses it. */
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

    /** The bytes of a mesh or of a texture, waiting for the render thread to copy them from
     *  staging into the buffer or image, which are preferably device-local. */
    struct PendingUpload
    {
        std::shared_ptr<Graphics::Vulkan::Buffer> staging;
        std::variant<std::shared_ptr<Graphics::Vulkan::Buffer>,
                     std::shared_ptr<Graphics::Vulkan::Texture>>
            destination;
    };

    const std::uint8_t mFramesInFlight;

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<const Graphics::Vulkan::Queue>       mGraphicsQueue;
    std::shared_ptr<const Graphics::Vulkan::Queue>       mTransferQueue;
    std::shared_ptr<Graphics::Vulkan::FencePool>         mFencePool;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::shared_ptr<Graphics::Vulkan::DescriptorHeap>    mHeap;
    std::unique_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::unique_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    /** Written by resize() on any thread. Extent is trivially copyable and 8 bytes, so the atomic
     * is lock-free (Renderer.cpp asserts it). */
    std::atomic<Extent> mRequestedExtent;

    /** Render thread only: the last requested extent that was applied. Surfaces that report their
     *  own size ignore the request, so this is not necessarily the size of the images. */
    Extent mAppliedExtent;

    /** Signaled by the transfer submit of every frame that uploads, waited by the graphics submit
     * of every frame. */
    std::shared_ptr<Graphics::Vulkan::TimelineSemaphore> mUploadTimeline;

    /** Render thread only: the last value the upload timeline was signaled with. Raised after the
     *  submit that signals it returned, so a submit that throws leaves it unchanged. It lives as
     *  long as the renderer, across stop() and start(), so it only ever grows. */
    std::uint64_t mUploadValue = 0u;

    /** Appended by createMesh() and createTexture() on any thread, copied and erased by the render
     * thread. Entries leave only after the submit that recorded them returned. */
    std::mutex                mUploadMutex;
    std::deque<PendingUpload> mPendingUploads;

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
