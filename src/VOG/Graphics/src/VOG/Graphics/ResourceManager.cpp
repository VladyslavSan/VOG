#include "VOG/Graphics/ResourceManager.hpp"

#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>

namespace VOG::Graphics
{
ResourceManager::ResourceManager(const std::shared_ptr<GraphicsProvider>& graphicsProvider)
    : mGraphicsProvider{graphicsProvider}
{
}

std::shared_ptr<Vulkan::Swapchain>
ResourceManager::createRenderSurface(const Common::JSONContainer& parameteters)
{
    return std::make_shared<Vulkan::Swapchain>(mGraphicsProvider, parameteters);
}
} // namespace VOG::Graphics
