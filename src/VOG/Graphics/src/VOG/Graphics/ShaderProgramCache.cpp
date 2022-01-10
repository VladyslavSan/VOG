#include "VOG/Graphics/ShaderProgramCache.hpp"

#include <VOG/Common/JSONContainer.hpp>
#include <VOG/Graphics/GraphicsProvider.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <fstream>

namespace VOG::Graphics
{
namespace
{
/** Shader config file name that should be present in shader home directory */
constexpr std::string_view kShaderConfigName = "config.json";

namespace ShaderStageKey
{
constexpr std::string_view kVertex   = "vertex";
constexpr std::string_view kFragment = "fragment";
} // namespace ShaderStageKey

std::string
readFile(const std::filesystem::path& path)
{
    std::string   data{};
    std::ifstream file{path, std::ios::binary};

    file.seekg(0u, std::ios::end);
    const std::size_t fileSize = file.tellg();
    file.seekg(0u, std::ios::beg);

    data.resize(fileSize, '\0');
    file.read(data.data(), fileSize);

    return data;
}

nlohmann::json
readJsonFile(const std::filesystem::path& path)
{
    VOG_ASSERT_MSG(std::filesystem::exists(path), "File must exist.");
    std::ifstream  jsonFile{path};
    nlohmann::json json{};
    jsonFile >> json;

    return json;
}

constexpr Vulkan::ShadingStage
toShaderStage(const std::string_view name)
{
    if (name == ShaderStageKey::kVertex)
    {
        return Vulkan::ShadingStage::eVertex;
    }
    else if (name == ShaderStageKey::kFragment)
    {
        return Vulkan::ShadingStage::eFragment;
    }

    throw std::invalid_argument{"Not a shading stage name."};
}
} // namespace

ShaderProgramCache::ShaderProgramCache(const GraphicsProvider&      graphicsProvider,
                                       const Common::JSONContainer& parameters)
    : mGraphicsProvider{graphicsProvider}
    , mShaderSourcePath{parameters["shader_source_dir"].getOr<std::string>("")}
    , mShaderConfig{readJsonFile(mShaderSourcePath / kShaderConfigName)}
    , mShaderMap{mShaderConfig.at("shaders")}
{
    const Vulkan::Device& device = mGraphicsProvider.getDevice();
    VOG_ASSERT_MSG(mShaderMap.is_object(), "Shader info entry should be json object.")
    for (auto shaderEntryIterator = mShaderMap.begin(); shaderEntryIterator != mShaderMap.end();
         ++shaderEntryIterator)
    {
        const std::string& shaderName = shaderEntryIterator.key();
        const auto&        shaderInfo = shaderEntryIterator.value();

        std::shared_ptr<Vulkan::ShaderProgram> program = std::make_shared<Vulkan::ShaderProgram>();
        for (auto currentStage = shaderInfo.begin(); currentStage != shaderInfo.end();
             ++currentStage)
        {
            const std::string&   shadingStageName = currentStage.key();
            Vulkan::ShadingStage shadingStage     = toShaderStage(shadingStageName);

            VOG_ASSERT_MSG(currentStage.value().is_string(),
                           "Value type of shading stage should be string.");
            const auto stagePath = mShaderSourcePath / currentStage.value();
            VOG_ASSERT_MSG(std::filesystem::exists(stagePath), "Shader path is invalid.");

            const std::string shaderSource = readFile(stagePath);
            VOG_ASSERT_MSG(!shaderSource.empty(), "Shader source file should not be empty.")

            auto shader = Vulkan::Shader::create(device, shadingStage, shaderSource);
            switch (shadingStage)
            {
            case Vulkan::ShadingStage::eVertex:
                program->vertexFunction = shader;
                break;
            case Vulkan::ShadingStage::eFragment:
                program->fragmentFunction = shader;
                break;
            }
        }

        VOG_ASSERT_MSG(program->vertexFunction && program->fragmentFunction,
                       "Program is incomplete.");

        mCache.emplace(shaderName, std::move(program));
    }
}

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
