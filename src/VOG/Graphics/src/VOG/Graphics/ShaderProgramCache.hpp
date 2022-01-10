#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace VOG::Common
{
class JSONContainer;
}

namespace VOG::Graphics::Vulkan
{
class ShaderProgram;
} // namespace VOG::Graphics::Vulkan

namespace VOG::Graphics
{
class GraphicsProvider;

class ShaderProgramCache
{
public:
    ShaderProgramCache(const GraphicsProvider&      graphicsProvider,
                       const Common::JSONContainer& parameters);

    std::shared_ptr<Vulkan::ShaderProgram> get(const std::string& name);

protected:
    const GraphicsProvider&               mGraphicsProvider;
    const std::filesystem::path           mShaderSourcePath;
    const nlohmann::json                  mShaderConfig;
    const nlohmann::json::const_reference mShaderMap;

    std::unordered_map<std::string, std::shared_ptr<Vulkan::ShaderProgram>> mCache;
};
} // namespace VOG::Graphics
