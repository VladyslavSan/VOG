#pragma once

#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Renderer/Shader.hpp>
#include <VOG/Engine/Renderer/Texture.hpp>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VOG::Engine
{
/**
 * Owns the Vulkan objects of one window and the thread that renders into it.
 *
 * An application has one Renderer. A Mesh, Material or Texture works only with the Renderer that
 * made it; using it with another is undefined.
 *
 * Two threads work with it. The main thread, or whichever thread calls submitFrame(), runs the
 * Renderables: it calls onDraw() for each frame and queues the result as a packet. The render
 * thread takes the packets and records and presents them, and it copies the meshes and textures
 * to the GPU; it runs no user code. The render thread starts in the constructor and ends in the
 * destructor. It does not wait for a frame to copy: a resource that was created is copied while
 * no frame is submitted too.
 *
 * The render thread owns nothing: it is the last member of the implementation and is joined
 * before anything it uses is destroyed. Nothing reachable from it owns the Renderer, so
 * `~Renderer` cannot run on the render thread.
 */
class Renderer final
{
    static constexpr std::uint64_t kBytesPerMebibyte = std::uint64_t{1} << 20u;

public:
    static constexpr std::uint8_t kMaxFramesInFlight = 3;

    /** The default of Config::resourceSlots. */
    static constexpr std::uint32_t kDefaultResourceSlots = 4096u;

    /** The default of Config::backgroundUploadBytesPerFrame: 64 MiB. */
    static constexpr std::uint64_t kDefaultBackgroundUploadBytes = 64u * kBytesPerMebibyte;

    /** Everything the renderer needs to bring up Vulkan and to find its shaders. */
    struct Config
    {
        std::string appName;
        std::string engineName = "VOG";

        /** Two frames in flight let the CPU record one frame while the GPU draws the one before. */
        std::uint8_t framesInFlight = 2u;

        /**
         * The shader directory: `Name.vert.glsl` and `Name.frag.glsl` for every shader, and the
         * includes of both in the subdirectory `include`. The engine's own shaders and its include
         * VOG/Draw.glsl are in src/VOG/Engine/resources/shaders.
         */
        std::filesystem::path shaderPath;

        /** Turns on the Khronos validation layer (VK_LAYER_KHRONOS_validation). The constructor
         *  throws if the layer is not installed. Without it the Renderer asks for no layer. */
        bool validation = false;

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

        /**
         * Images and parameter blocks the descriptor heap holds at once. The Renderer uses one
         * slot for the Frame block of each frame slot; the rest is for textures and materials.
         * When the heap is full, createTexture() and createMaterial() throw. The size is fixed:
         * growing means binding another heap.
         */
        std::uint32_t resourceSlots = kDefaultResourceSlots;

        /**
         * Bytes of copies that the render thread records in one iteration for resources that no
         * draw of that iteration uses. A resource that a frame draws is always copied in the
         * iteration of that frame, whatever this is. It bounds how long one submit to the transfer
         * queue runs; it is not a guarantee of bandwidth. The entry that reaches the limit is
         * recorded, so a resource bigger than the limit still goes through, alone. 0 copies only
         * what a draw uses.
         */
        std::uint64_t backgroundUploadBytesPerFrame = kDefaultBackgroundUploadBytes;
    };

    enum class State : std::uint8_t
    {
        eRunning,
        /** The render thread ended with an exception; failure() returns it. Final: destroy the
         *  Renderer. createMesh() and createTexture() still queue after that, and nothing
         *  drains the queue: a thread that imports resources should stop when getState() is
         *  eFailed. */
        eFailed
    };

    /** What the render thread has not yet copied to the GPU. */
    struct UploadStatus
    {
        std::size_t   pendingCount = 0u;
        std::uint64_t pendingBytes = 0u;
    };

    /**
     * Brings Vulkan up, compiles and checks every shader of the shader directory, then starts the
     * render thread, last: a shader error throws before any thread exists.
     * @throws std::runtime_error (nested) naming the shader and file that failed to compile, link
     *         or follow VOG/Draw.glsl.
     */
    Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config);

    /**
     * Joins the render thread, then takes the GPU state down. A failure nobody read with failure()
     * is logged. Must not run while another thread is inside any member of this Renderer: join
     * the threads that create resources first. A handle of a mesh, texture or material may outlive
     * the Renderer.
     */
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
     * copies them into the memory of the mesh, preferably device-local, on the transfer queue: in
     * the iteration of the first frame that draws the mesh, or in the background, within
     * Config::backgroundUploadBytesPerFrame, while no frame is submitted too. A draw of the mesh
     * before the copy has finished waits for it on the GPU; Mesh::isResident() tells. Any thread:
     * the allocator is internally synchronized. After a failure of the render thread (getState()
     * is eFailed) the mesh is queued and never copied.
     * @throws std::invalid_argument if the description is empty, the stride is zero, or its size is
     *         not a multiple of the stride; the layout has more attributes than a pipeline can
     *         take; an attribute overruns the stride; the stride or an attribute offset is not a
     *         multiple of 4, or exceeds maxVertexInputBindingStride or
     *         maxVertexInputAttributeOffset of the device; its index count is not a multiple of 3;
     *         or an index is not less than the vertex count. The name of the description starts
     *         the message.
     */
    [[nodiscard]] Mesh createMesh(const MeshDescription& description);

    /**
     * Copies the pixels of @p description. Any thread: the allocator is internally synchronized.
     * The render thread uploads the image like the vertices of a mesh, on the transfer queue;
     * Texture::isResident() tells when it has finished. After a failure of the render thread
     * (getState() is eFailed) the texture is queued and never copied.
     * @throws std::invalid_argument if the extent is zero or exceeds maxImageDimension2D of the
     *         device, or the pixels are not exactly width * height * 4 bytes.
     * @throws std::runtime_error if the renderer's descriptor heap is full or the image cannot be
     *         created.
     */
    [[nodiscard]] Texture createTexture(const TextureDescription& description);

    /**
     * The shader @p name of the shader directory, which was compiled and checked when the
     * Renderer was made. Any thread: the shaders are only read.
     * @throws std::invalid_argument if the directory has no shader of that name.
     */
    [[nodiscard]] Shader getShader(std::string_view name) const;

    /**
     * Checks the parameters of @p description against the Material block of its shader and copies
     * them into a uniform block, written once. Any thread: the shaders are only read, and the
     * allocator is internally synchronized.
     * @throws std::invalid_argument if the shader is empty, a parameter is not a member of the
     *         Material block, is given twice, has another type than the member or is an empty
     *         texture, or a member has no value. A name in the description starts the message.
     * @throws std::runtime_error if the renderer's descriptor heap is full.
     */
    [[nodiscard]] Material createMaterial(MaterialDescription description);

    /**
     * Whether @p mesh provides every vertex input of the shader of @p material; the error names
     * the mesh, the shader and the first input that is missing. An empty mesh or material is
     * compatible: it draws nothing. RenderQueue::submit() makes the same check. Any thread; it
     * changes nothing.
     */
    [[nodiscard]] std::expected<void, std::string> checkDraw(const Mesh&     mesh,
                                                             const Material& material) const;

    /**
     * What is queued for the render thread and was not submitted to the GPU yet: a progress bar,
     * not an answer for one resource (Mesh::isResident() is that). pendingBytes is also the
     * staging memory that the queue holds. Any thread.
     */
    [[nodiscard]] UploadStatus uploads() const noexcept;

    /**
     * New window size in pixels. Any thread; the render thread recreates the swapchain at that
     * size before its next frame. Surfaces that report their own size (macOS, Windows, X11) ignore
     * it; Wayland and headless surfaces take it as given.
     */
    void resize(Extent extent) noexcept;

    /**
     * Calls Renderable::onDraw() of every renderable on the calling thread and queues the draws
     * with the camera view an onDraw() set as one frame. Waits while the render thread still holds
     * the previous frame, so this paces the caller to the render thread. When the render thread
     * has failed it still calls onDraw(), and the frame is dropped; check getState().
     *
     * One thread at a time, and not from onDraw(). An exception from onDraw(), also a DrawError
     * from RenderQueue::submit(), reaches the caller. The frame is not queued then and the
     * renderer keeps running. The renderables that ran before the one that threw have run for a
     * frame that is dropped.
     */
    void submitFrame(const FrameContext& frameContext);

    /**
     * Like submitFrame(), but never waits for the render thread: if it still holds the previous
     * frame, returns at once without calling onDraw(). For callers that must not block on the
     * render thread, like an event watch.
     */
    void trySubmitFrame(const FrameContext& frameContext);

    /** Any thread. */
    [[nodiscard]] State getState() const noexcept;

    /**
     * The exception that ended the render thread, or null if it runs. Every call returns it.
     * Calling it marks the failure as collected, so ~Renderer does not log what the caller has
     * seen. Any thread.
     */
    [[nodiscard]] std::exception_ptr failure() noexcept;

private:
    /** Lets RendererTest read the parts of the implementation; nothing else uses it. */
    friend struct RendererTestAccess;

    /** The components of the renderer. It names Graphics types, so it stays out of this header. */
    struct Impl;

    std::unique_ptr<Impl> mImpl;
};
} // namespace VOG::Engine
