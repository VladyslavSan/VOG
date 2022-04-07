#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

#include <boost/thread/scoped_thread.hpp>

#include <memory>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics
{
class GraphicsProvider;
class ResourceManager;
} // namespace VOG::Graphics

namespace VOG::Graphics::Frame
{
class FrameObjectManager;
}

namespace VOG::Graphics::Vulkan
{
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
    static constexpr std::uint8_t MaxFramesInFlight = 3;

    enum class RenderJobState : uint8_t
    {
        eInactive = 0,
        eActive
    };

    virtual ~Renderer();

    Renderer(const Common::JSONContainer& parameters);

    Renderer(const Renderer&)  = delete;
    Renderer(Renderer&& other) = delete;

    void render();

    const std::shared_ptr<Scene::Scene>& getScene() const;

    RenderJobState getRenderJobState() const;

    bool requestRenderChangeState(RenderJobState newState);

protected:
    std::size_t getFrameInFlightIndex() const;

protected:
    /// RenderThread run function
    static void renderThreadMain(std::weak_ptr<Renderer> renderer);

    ThreadType                  mRenderThread;
    std::atomic<RenderJobState> mCurrentState;

    std::uint8_t             mMaxFramesInFlight;
    std::atomic<std::size_t> mRenderFrame;

    std::shared_ptr<Graphics::GraphicsProvider>          mGraphicsProvider;
    std::shared_ptr<Graphics::ResourceManager>           mResourceManager;
    std::shared_ptr<Graphics::Vulkan::Swapchain>         mSwapchain;
    std::shared_ptr<Graphics::Frame::FrameObjectManager> mFrameObjectManager;

    std::shared_ptr<Scene::Scene> mScene;
};
} // namespace VOG::Engine
