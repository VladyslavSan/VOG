#include "VOG/Graphics/ShaderProgramCache.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <glslang/Public/ShaderLang.h>
#include <nlohmann/json.hpp>

#include <format>
#include <fstream>

namespace VOG::Graphics
{
namespace
{
/** Shader config file name that should be present in shader home directory */
constexpr std::string_view gShaderConfigName = "config.json";

namespace ShaderStageKey
{
constexpr std::string_view gVertex   = "vertex";
constexpr std::string_view gFragment = "fragment";
} // namespace ShaderStageKey

std::string
readFile(const std::filesystem::path& path)
{
    std::string   data{};
    std::ifstream file{path, std::ios::binary};

    file.seekg(0u, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0u, std::ios::beg);

    data.resize(fileSize, '\0');
    file.read(data.data(), fileSize);

    return data;
}

nlohmann::json
readJsonFile(const std::filesystem::path& path)
{
    std::ifstream  jsonFile{path};
    nlohmann::json json{};
    jsonFile >> json;

    return json;
}

constexpr Vulkan::Shader::ShadingStage
toShaderStage(const std::string_view name)
{
    if (name == ShaderStageKey::gVertex)
    {
        return Vulkan::Shader::ShadingStage::eVertex;
    }
    if (name == ShaderStageKey::gFragment)
    {
        return Vulkan::Shader::ShadingStage::eFragment;
    }

    throw std::invalid_argument{"Not a shading stage name."};
}
} // namespace

ShaderProgramCache::ShaderProgramCache(const Vulkan::DevicePtr&     device,
                                       const std::filesystem::path& shaderSourcePath)
    : mDevice{device}
    , mShaderSourcePath{shaderSourcePath}
{
    glslang::InitializeProcess();

    if (!std::filesystem::exists(shaderSourcePath) ||
        !std::filesystem::exists(shaderSourcePath / gShaderConfigName))
    {
        throw std::runtime_error{"Shader source path does not exist."};
    }

    const nlohmann::json shaderConfig         = readJsonFile(mShaderSourcePath / gShaderConfigName);
    nlohmann::json::const_reference shaderMap = shaderConfig.at("shaders");

    VOG_ASSERT_MSG(shaderMap.is_object(), "Shader info entry should be json object.")
    for (auto shaderEntryIterator = shaderMap.begin(); shaderEntryIterator != shaderMap.end();
         ++shaderEntryIterator)
    {
        const std::string& shaderName = shaderEntryIterator.key();
        const auto&        shaderInfo = shaderEntryIterator.value();

        Vulkan::ShaderProgram::ShadingStages stages{};
        for (auto currentStage = shaderInfo.begin(); currentStage != shaderInfo.end();
             ++currentStage)
        {
            const std::string&           shadingStageName = currentStage.key();
            Vulkan::Shader::ShadingStage shadingStage     = toShaderStage(shadingStageName);

            VOG_ASSERT_MSG(currentStage.value().is_string(),
                           "Value type of shading stage should be string.");
            const auto stagePath = mShaderSourcePath / currentStage.value();
            VOG_ASSERT_MSG(std::filesystem::exists(stagePath), "Shader path is invalid.");

            const std::string shaderSource = readFile(stagePath);
            VOG_ASSERT_MSG(!shaderSource.empty(), "Shader source file should not be empty.")

            std::shared_ptr<Vulkan::Shader> shader = nullptr;
            try
            {
                shader = Vulkan::Shader::create(device, shadingStage, shaderSource);
            }
            catch (const std::exception&)
            {
                std::throw_with_nested(std::runtime_error{std::format(
                    "Failed to compile shader \"{}\" ({})", shaderName, stagePath.string())});
            }
            switch (shadingStage)
            {
            case Vulkan::Shader::ShadingStage::eVertex:
                stages.vertex = {.shader = shader, .entryPoint = "main"};
                break;
            case Vulkan::Shader::ShadingStage::eFragment:
                stages.fragment = {.shader = shader, .entryPoint = "main"};
                break;
            }
        }

        std::shared_ptr<Vulkan::ShaderProgram> program =
            std::make_shared<Vulkan::ShaderProgram>(std::move(stages));

        mCache.emplace(shaderName, std::move(program));
    }
}

ShaderProgramCache::~ShaderProgramCache() { glslang::FinalizeProcess(); }

std::shared_ptr<Vulkan::ShaderProgram>
ShaderProgramCache::get(const std::string& name)
{
    const auto found = mCache.find(name);
    if (found == mCache.end()) [[unlikely]]
    {
        throw std::invalid_argument{"Requested shader not found."};
    }

    return found->second;
}
} // namespace VOG::Graphics
