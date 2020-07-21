#pragma once

#include <VOG/Common/NoCopyable.hpp>
#include <VOG/Scene/Scene.hpp>

#include <functional>
#include <memory>
#include <thread>
#include <unordered_map>

namespace vk::raii
{
class Fence;
class Semaphore;
} // namespace vk::raii

namespace VOG
{

namespace Common
{
class JSONContainer;
}

namespace Graphics
{

namespace Api
{
class GraphicsProvider;
class Device;
class CommandsSubmission;
class CommandManager;
} // namespace Api

namespace Resources
{
class RenderSurface;
class ResourceManager;
} // namespace Resources
} // namespace Graphics

namespace Engine
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

    Renderer(const Renderer&) = delete;
    Renderer(Renderer&& other) = default;

    Renderer& operator=(const Renderer&) = delete;
    Renderer& operator=(Renderer&&) = default;

    void Render();

    const std::shared_ptr<Scene::Scene>& getScene() const;

    RenderJobState GetCurrentRenderState() const;

    RenderJobState GetNextRenderState() const;

    bool RequestRenderChangeState(RenderJobState newState);

protected:
    std::size_t GetFrameInFlightIndex() const;

protected:
    /// RenderThread run function
    static void Run(std::weak_ptr<Renderer> renderer);

    std::thread m_renderThread;
    std::atomic<RenderJobState> m_currentState;
    std::atomic<RenderJobState> m_nextState;

    std::shared_ptr<Graphics::Api::GraphicsProvider> m_graphicsProvider;
    std::shared_ptr<Graphics::Resources::ResourceManager> m_resourceManager;
    std::shared_ptr<Graphics::Api::Device> m_device;
    std::shared_ptr<Graphics::Resources::RenderSurface> m_renderSurface;

    std::shared_ptr<VOG::Scene::Scene> m_scene;

    std::uint8_t m_maxFramesInFlight;
    std::atomic<std::size_t> m_renderFrame;
    std::unique_ptr<Graphics::Api::CommandManager> m_commandManager;
    std::vector<std::vector<std::unique_ptr<Graphics::Api::CommandsSubmission>>> m_submissions;
};
} // namespace Engine
} // namespace VOG
