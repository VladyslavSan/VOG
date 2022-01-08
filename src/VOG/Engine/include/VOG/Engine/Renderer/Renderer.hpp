#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

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
public:
    static constexpr std::uint8_t MaxFramesInFlight = 3;

    enum class RenderJobState : uint8_t
    {
        Inactive = 0,
        Active
    };

    virtual ~Renderer();

    Renderer(const Common::JSONContainer& parameters);

    Renderer(const Renderer&)  = delete;
    Renderer(Renderer&& other) = delete;

    void Render();

    const std::shared_ptr<Scene::Scene>& getScene() const;

    RenderJobState GetCurrentRenderState() const;

    RenderJobState GetNextRenderState() const;

    bool RequestRenderChangeState(RenderJobState newState);

protected:
    std::size_t GetFrameInFlightIndex() const;

protected:
    /// RenderThread run function
    static void Run(std::stop_token stop_token, std::weak_ptr<Renderer> renderer);

    std::jthread                mRenderThread;
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
