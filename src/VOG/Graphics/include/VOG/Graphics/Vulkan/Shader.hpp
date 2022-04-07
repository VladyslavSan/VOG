#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <boost/container/small_vector.hpp>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace VOG::Graphics::Vulkan
{
class Device;

enum class ShadingStage : std::uint8_t
{
    eVertex   = 0,
    eFragment = 1,
    eUnknown
};

class Shader
{
public:
    class CompilationError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct Reflection
    {
        enum class ResourceType : std::uint8_t
        {
            eUniformBuffer = 0,
            eTexture
        };

        struct ResourceLocation
        {
            std::uint32_t set;
            std::uint32_t binding;
        };

        struct UniformBuffer
        {
            ResourceLocation location;
            std::size_t      size;
        };

        boost::container::small_vector<UniformBuffer, 2> uniformBuffers;
        boost::container::small_vector<UniformBuffer, 2> storageBuffers;
    };

    static std::shared_ptr<Shader>
    create(const Device& device, ShadingStage stage, const std::string& source);

    /**
     * Construct from glsl shader code. Very expensive as it involves glsl to spirv compilation.
     *
     * @param device Vulkan device to construct the shader module.
     * @param stage Shading stage.
     * @param shaderBinary SPIR-V shader binary code.
     */
    Shader(const Device& device, ShadingStage stage, const std::string& glslCode);

    /**
     * Construct from SPIR-V shader binary.
     *
     * @param device Vulkan device to construct the shader module.
     * @param stage Shading stage.
     * @param shaderBinary SPIR-V shader binary code.
     */
    Shader(const Device& device, ShadingStage stage, std::vector<std::uint32_t> shaderBinary);

    Reflection reflect() const;

    const ShadingStage               stage;
    const std::vector<std::uint32_t> binary;
    const vk::raii::ShaderModule     module;
};
using ShaderPtr = std::shared_ptr<Shader>;
} // namespace VOG::Graphics::Vulkan
