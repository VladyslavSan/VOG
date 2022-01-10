#pragma once

#include <memory>
#include <string>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Vulkan
{
class Swapchain;
class ShaderProgram;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Graphics
{
class GraphicsProvider;
class ShaderProgramCache;

class ResourceManager
{

public:
    ResourceManager(const std::shared_ptr<GraphicsProvider>& GraphicsProvider,
                    const Common::JSONContainer&             parameters);
    ~ResourceManager();

    std::shared_ptr<Vulkan::Swapchain>
    createRenderSurface(const Common::JSONContainer& parameteters);

    std::shared_ptr<Vulkan::ShaderProgram> createShaderProgram(const std::string& name);

protected:
    std::shared_ptr<GraphicsProvider>   mGraphicsProvider;
    std::unique_ptr<ShaderProgramCache> mShaderProgramCache;
};
} // namespace VOG::Graphics
