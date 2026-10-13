#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
class Renderer
    : public std::enable_shared_from_this<Renderer>
    , public VOG::Common::NoCopyable
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

    enum class RenderJobState : uint8_t
    {
        eInactive = 0,
        eActive
    };

    virtual ~Renderer();

    Renderer(const Common::SurfaceHandle& surfaceHandle, const Config& config);

    Renderer(const Renderer&)  = delete;
    Renderer(Renderer&& other) = delete;

    void render();

    /**
     * Registers @p renderable; its prepare() runs on the render thread before its first draw.
     *
     * @note Do not call from inside Renderable::prepare() or Renderable::collect(): the renderer
     * holds the registration lock while it runs them, so the call deadlocks.
     */
    void addRenderable(std::shared_ptr<Renderable> renderable);

    RenderJobState getRenderJobState() const;

    bool requestRenderChangeState(RenderJobState newState);

protected:
    struct RenderableEntry
    {
        std::shared_ptr<Renderable> renderable;
        bool                        prepared = false;
    };

    /** Runs prepare() on renderables registered since the last call. */
    void prepareRenderables();

    /** Refills mRenderItems with the draws of this frame. */
    void collectRenderItems(const FrameContext& frameContext);

    /** Serializes state transitions and render thread start/stop. */
    std::mutex                  mStateMutex;
    std::jthread                mRenderThread;
    std::atomic<RenderJobState> mCurrentState;

    std::uint8_t mMaxFramesInFlight;

    /** Passed to the swapchain at creation and at every recreation. */
    vk::Extent2D mExtent;

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<const Graphics::Vulkan::Queue>       mGraphicsQueue;
    std::shared_ptr<Graphics::Vulkan::FencePool>         mFencePool;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::unique_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::shared_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    /** Guards mRenderables against registration from other threads. */
    std::mutex                   mRenderablesMutex;
    std::vector<RenderableEntry> mRenderables;

    /** Render-thread scratch space, kept to reuse its capacity across frames. */
    std::vector<RenderItem> mRenderItems;

    std::chrono::steady_clock::time_point mStartTime;
};
} // namespace VOG::Engine
