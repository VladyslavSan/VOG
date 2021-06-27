#include "VOG/Engine/Renderer.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Resources/RenderSurface.hpp>
#include <VOG/Graphics/Resources/ResourceManager.hpp>
#include <VOG/Graphics/Resources/Shader.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/VulkanWrappers/CommandManager.hpp>
#include <VOG/Graphics/VulkanWrappers/Device.hpp>

namespace VOG::Engine
{
Renderer::~Renderer()
{
    RequestRenderChangeState(RenderJobState::Inactive);
    if (m_renderThread.joinable())
        m_renderThread.join();
}

Renderer::Renderer(const Common::JSONContainer& parameters)
    : m_renderThread{}
    , m_currentState{RenderJobState::Inactive}
    , m_nextState{RenderJobState::Inactive}
    , m_graphicsProvider{std::make_shared<Graphics::Api::GraphicsProvider>(parameters)}
    , m_resourceManager{std::make_shared<Graphics::Resources::ResourceManager>(m_graphicsProvider)}
    , m_device{std::make_shared<VOG::Graphics::VulkanWrappers::Device>(m_graphicsProvider)}
    , m_renderSurface{m_resourceManager->CreateRenderSurface(parameters)}
    , m_scene{std::make_shared<Scene::Scene>()}
    , m_maxFramesInFlight{std::clamp(
          static_cast<std::uint8_t>(
              parameters["frames_in_flight"]->GetValueOr<Common::JSONContainer::UnsignedInt>(1)),
          std::uint8_t{1}, MaxFramesInFlight)}
    , m_renderFrame{0}
    , m_commandManager{std::make_shared<Graphics::VulkanWrappers::CommandManager>(
          m_graphicsProvider, m_maxFramesInFlight)}
{
    m_submissions.resize(m_maxFramesInFlight);
}

void
Renderer::Render()
{
    if (!m_renderSurface->AcquireNextImage())
        throw std::runtime_error{"Could not acquire image"};

    const std::size_t frameInFlightIndex = GetFrameInFlightIndex();
    auto& submissions = m_submissions[frameInFlightIndex];
    for (auto& submission : submissions)
    {
        submission.WaitUntilFinished();
    }
    submissions.clear();

    auto commandRequest = m_commandManager->Request(frameInFlightIndex);
    commandRequest.ResetPool();

    m_device->UseCommandBuffer(commandRequest.MakeCommandBuffer());

    m_device->BeginRenderPass(m_renderSurface, nullptr);

    m_scene->drawScene(*this);

    m_device->EndRenderPass();

    auto commandBuffer = m_device->EndCommandBuffer();
    submissions.push_back(commandRequest.SubmitCommandBuffer(std::move(commandBuffer)));
    m_renderSurface->Present(submissions[0].GetSemaphore());

    m_device->OnFrameReset();

    ++m_renderFrame;
}

const std::shared_ptr<Scene::Scene>&
Renderer::getScene() const
{
    return m_scene;
}

Renderer::RenderJobState
Renderer::GetCurrentRenderState() const
{
    return m_currentState.load();
}

Renderer::RenderJobState
Renderer::GetNextRenderState() const
{
    return m_nextState.load();
}

bool
Renderer::RequestRenderChangeState(RenderJobState newState)
{
    if (newState == m_currentState.load() || newState == m_nextState.load())
        return false;

    m_nextState.store(newState);

    switch (newState)
    {
    case RenderJobState::Active:
    {
        m_renderThread = std::thread{&Renderer::Run, weak_from_this()};
        break;
    }
    case RenderJobState::Inactive:
    {
        break;
    }
    default:
        break;
    }

    return true;
}

std::size_t
Renderer::GetFrameInFlightIndex() const
{
    return m_renderFrame.load() % m_maxFramesInFlight;
}

void
Renderer::Run(std::weak_ptr<Renderer> renderer)
{
    {
        auto locked = renderer.lock();
        if (locked)
            locked->m_currentState.store(locked->m_nextState.load());
    }

    while (true)
    {
        auto locked = renderer.lock();
        if (!locked || locked->m_nextState.load() != RenderJobState::Active)
            break;

        locked->Render();
    }

    auto locked = renderer.lock();
    if (!locked)
        return;

    locked->m_currentState.store(locked->m_nextState.load());
}
} // namespace VOG::Engine
