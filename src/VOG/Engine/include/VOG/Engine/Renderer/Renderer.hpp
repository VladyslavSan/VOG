#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Common/SurfaceHandle.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

#include <boost/thread/scoped_thread.hpp>

#include <memory>

namespace VOG::Common
{
class JSONContainer;
}

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
class GraphicsPipeline;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Engine
{
class Renderer
    : public std::enable_shared_from_this<Renderer>
    , public VOG::Common::NoCopyable
{
    using ThreadType = boost::scoped_thread<boost::interrupt_and_join_if_joinable, boost::thread>;

public:
    static constexpr std::uint8_t kMaxFramesInFlight = 3;

    enum class RenderJobState : uint8_t
    {
        eInactive = 0,
        eActive
    };

    virtual ~Renderer();

    Renderer(const Common::SurfaceHandle& surfaceHandle, const Common::JSONContainer& parameters);

    Renderer(const Renderer&)  = delete;
    Renderer(Renderer&& other) = delete;

    void render();

    const std::shared_ptr<Scene::Scene>& getScene() const;

    RenderJobState getRenderJobState() const;

    bool requestRenderChangeState(RenderJobState newState);

protected:
    /// RenderThread run function
    static void renderThreadMain(const std::weak_ptr<Renderer>& renderer);

    ThreadType                  mRenderThread;
    std::atomic<RenderJobState> mCurrentState;

    std::uint8_t mMaxFramesInFlight;

    std::shared_ptr<Graphics::Vulkan::Instance>          mVulkanInstance;
    std::shared_ptr<Graphics::Vulkan::Device>            mVulkanDevice;
    std::shared_ptr<Graphics::ShaderProgramCache>        mShaderProgramCache;
    std::shared_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::shared_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    std::shared_ptr<Scene::Scene> mScene;
};
} // namespace VOG::Engine
