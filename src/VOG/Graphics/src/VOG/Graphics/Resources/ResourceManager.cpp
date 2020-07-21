#include "VOG/Graphics/Resources/ResourceManager.hpp"

#include <VOG/Graphics/Resources/RenderSurface.hpp>

namespace VOG::Graphics::Resources
{
ResourceManager::ResourceManager(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider)
    : m_graphicsProvider{graphicsProvider}
{
}

std::shared_ptr<RenderSurface>
ResourceManager::CreateRenderSurface(const Common::JSONContainer& parameteters)
{
    return std::make_shared<RenderSurface>(m_graphicsProvider, parameteters);
}
} // namespace VOG::Graphics::Resources
