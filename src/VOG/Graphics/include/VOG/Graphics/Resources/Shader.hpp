#pragma once

#include <VOG/Graphics/Typedefs.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace vk::raii
{
class ShaderModule;
}

namespace VOG::Graphics
{
namespace Api
{
class GraphicsProvider;
}

namespace Resources
{
class Shader
{
public:
    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Unknown
    };

    Shader(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider, std::string shaderName,
           std::filesystem::path filepath, std::vector<std::filesystem::path> includeDirectories);

    Shader(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider, std::string shaderName,
           ShaderStage stage, std::string shaderCode);

    ShaderStage getStage() const;

private:
    std::shared_ptr<Api::GraphicsProvider> m_graphicsProvider;

    ShaderStage m_shaderStage;
    std::string m_shaderName;
    std::string m_shaderSource;
    std::string m_shaderPreprocessed;
    std::string m_shaderBinary;

    std::shared_ptr<vk::raii::ShaderModule> m_shaderModule;
};

class ShaderCompilationError : std::runtime_error
{
public:
    enum class CompilationStage
    {
        PreprocessGLSL,
        GLSLtoSPIRV
    };

    ShaderCompilationError(std::string shaderName, CompilationStage stage,
                           std::string shaderCompilationError);

protected:
};

VOG_DECLARE_PTR(Shader);
} // namespace Resources
} // namespace VOG::Graphics
