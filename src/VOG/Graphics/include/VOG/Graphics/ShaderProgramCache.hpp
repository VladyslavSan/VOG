#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(ShaderProgram);
VOG_DECLARE_PTR(Device);

} // namespace VOG::Graphics::Vulkan

namespace VOG::Graphics
{
class ShaderProgramCache
{
public:
    ShaderProgramCache(const Vulkan::DevicePtr&     device,
                       const std::filesystem::path& shaderSourcePath);

    ~ShaderProgramCache();

    Vulkan::ShaderProgramPtr get(const std::string& name);

protected:
    Vulkan::DevicePtr           mDevice;
    const std::filesystem::path mShaderSourcePath;

    std::unordered_map<std::string, Vulkan::ShaderProgramPtr> mCache;
};
} // namespace VOG::Graphics
