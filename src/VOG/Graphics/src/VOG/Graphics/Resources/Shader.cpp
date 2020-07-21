#include "VOG/Graphics/Resources/Shader.hpp"

#include <VOG/Graphics/Api/GraphicsProvider.hpp>
#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <shaderc/shaderc.hpp>

#include <filesystem>
#include <mutex>
#include <stdexcept>

namespace VOG::Graphics::Resources
{
namespace
{
Shader::ShaderStage
ResovleShaderStage(const std::filesystem::path& path)
{
    if (!path.has_extension())
        return Shader::ShaderStage::Unknown;

    if (path.extension() == "vert")
        return Shader::ShaderStage::Vertex;
    else if (path.extension() == "frag")
        return Shader::ShaderStage::Fragment;

    return Shader::ShaderStage::Unknown;
}

shaderc_shader_kind
ConvertShaderStage(Shader::ShaderStage stage)
{
    switch (stage)
    {
    case VOG::Graphics::Resources::Shader::ShaderStage::Vertex:
        return shaderc_vertex_shader;
        break;
    case VOG::Graphics::Resources::Shader::ShaderStage::Fragment:
        return shaderc_fragment_shader;
        break;
    }

    return shaderc_glsl_infer_from_source;
}

std::string
ConvertShaderCompilationStage(ShaderCompilationError::CompilationStage stage)
{
    switch (stage)
    {
    case VOG::Graphics::Resources::ShaderCompilationError::CompilationStage::PreprocessGLSL:
        return "PreprocessGLSL";
        break;
    case VOG::Graphics::Resources::ShaderCompilationError::CompilationStage::GLSLtoSPIRV:
        return "GLSLtoSPIRV";
        break;
    default:
        break;
    }

    return "Unknown";
}
} // namespace

Shader::Shader(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
               std::string shaderName, std::filesystem::path filepath,
               std::vector<std::filesystem::path> includeDirectories)
    : m_graphicsProvider{graphicsProvider}
    , m_shaderName{std::move(shaderName)}
    , m_shaderStage{ResovleShaderStage(filepath)}
{
}

Shader::Shader(const std::shared_ptr<Api::GraphicsProvider>& graphicsProvider,
               std::string shaderName, ShaderStage stage, std::string shaderCode)
    : m_graphicsProvider{graphicsProvider}
    , m_shaderName{std::move(shaderName)}
    , m_shaderStage{stage}
    , m_shaderSource{std::move(shaderCode)}
{
    shaderc::Compiler compiler;
    shaderc::CompileOptions options;

    shaderc::PreprocessedSourceCompilationResult resultPreprocessor = compiler.PreprocessGlsl(
        m_shaderSource, ConvertShaderStage(m_shaderStage), m_shaderName.c_str(), options);

    if (resultPreprocessor.GetCompilationStatus() != shaderc_compilation_status_success)
        throw ShaderCompilationError(m_shaderName,
                                     ShaderCompilationError::CompilationStage::PreprocessGLSL,
                                     resultPreprocessor.GetErrorMessage());

    m_shaderPreprocessed = {resultPreprocessor.cbegin(), resultPreprocessor.cend()};

    shaderc::AssemblyCompilationResult resultCompile = compiler.CompileGlslToSpvAssembly(
        m_shaderPreprocessed, ConvertShaderStage(m_shaderStage), m_shaderName.c_str(), options);

    if (resultCompile.GetCompilationStatus() != shaderc_compilation_status_success)
        throw ShaderCompilationError(m_shaderName,
                                     ShaderCompilationError::CompilationStage::GLSLtoSPIRV,
                                     resultCompile.GetErrorMessage());

    m_shaderBinary = {resultCompile.cbegin(), resultCompile.cend()};

    m_shaderModule = std::make_shared<vk::raii::ShaderModule>(
        *m_graphicsProvider->GetDevice(),
        vk::ShaderModuleCreateInfo{
            {}, m_shaderBinary.size(), reinterpret_cast<std::uint32_t*>(m_shaderBinary.data())});
}

Shader::ShaderStage
Shader::getStage() const
{
    return m_shaderStage;
}

ShaderCompilationError::ShaderCompilationError(std::string shaderName,
                                               ShaderCompilationError::CompilationStage stage,
                                               std::string shaderCompilationError)
    : std::runtime_error{"Compilation failed for shader=" + shaderName +
                         " on stage=" + ConvertShaderCompilationStage(stage) +
                         " with error=" + shaderCompilationError}
{
}
} // namespace VOG::Graphics::Resources
