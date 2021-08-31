#pragma once

#include <memory>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Vulkan
{
class Swapchain;
}

namespace VOG::Graphics
{
class GraphicsProvider;

class ResourceManager
{
public:
    ResourceManager(const std::shared_ptr<GraphicsProvider>& GraphicsProvider);

    std::shared_ptr<Vulkan::Swapchain>
    createRenderSurface(const Common::JSONContainer& parameteters);

protected:
    std::shared_ptr<GraphicsProvider> mGraphicsProvider;
};
} // namespace VOG::Graphics
