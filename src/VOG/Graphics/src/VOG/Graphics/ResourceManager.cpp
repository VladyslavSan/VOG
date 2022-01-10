#include "VOG/Graphics/ResourceManager.hpp"

#include <VOG/Graphics/Vulkan/Attachment/Swapchain.hpp>

#include "VOG/Graphics/ShaderProgramCache.hpp"

namespace VOG::Graphics
{
ResourceManager::ResourceManager(const std::shared_ptr<GraphicsProvider>& graphicsProvider,
                                 const Common::JSONContainer&             parameters)
    : mGraphicsProvider{graphicsProvider}
    , mShaderProgramCache{std::make_unique<ShaderProgramCache>(*graphicsProvider, parameters)}
{
}

ResourceManager::~ResourceManager() {}

std::shared_ptr<Vulkan::Swapchain>
ResourceManager::createRenderSurface(const Common::JSONContainer& parameteters)
{
    return std::make_shared<Vulkan::Swapchain>(mGraphicsProvider, parameteters);
}
std::shared_ptr<Vulkan::ShaderProgram>
ResourceManager::createShaderProgram(const std::string& name)
{
    return mShaderProgramCache->get(name);
}
} // namespace VOG::Graphics