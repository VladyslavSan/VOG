#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>

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

    /** Registers @p renderable; its prepare() runs on the render thread before its first draw. */
    void addRenderable(std::shared_ptr<Renderable> renderable);

    void clearRenderables();

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

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::shared_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::shared_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    /** Guards mRenderables against registration from other threads. */
    std::mutex                   mRenderablesMutex;
    std::vector<RenderableEntry> mRenderables;

    /** Render-thread scratch space, kept to reuse its capacity across frames. */
    std::vector<RenderItem> mRenderItems;

    std::chrono::steady_clock::time_point mStartTime;
    std::uint64_t                         mFrameIndex = 0u;
};
} // namespace VOG::Engine
