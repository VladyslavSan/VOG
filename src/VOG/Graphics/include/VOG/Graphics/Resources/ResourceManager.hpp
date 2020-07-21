#pragma once

#include <memory>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics
{

namespace Api
{
class GraphicsProvider;
}

namespace Resources
{
class RenderSurface;

class ResourceManager
{
public:
    ResourceManager(const std::shared_ptr<Api::GraphicsProvider>& GraphicsProvider);

    std::shared_ptr<Resources::RenderSurface>
    CreateRenderSurface(const Common::JSONContainer& parameteters);

protected:
    std::shared_ptr<Api::GraphicsProvider> m_graphicsProvider;
};
} // namespace Resources
} // namespace VOG::Graphics
