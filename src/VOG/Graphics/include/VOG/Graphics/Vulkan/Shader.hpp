#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <boost/container/static_vector.hpp>

#include <memory>
#include <optional>
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
    template <class T, std::size_t N>
    using Container = boost::container::static_vector<T, N>;

    class CompilationError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    struct Reflection
    {
        enum class ResourceType : std::uint8_t
        {
            eVertexBuffer = 0,
            eUniformBuffer,
            eTexture
        };

        struct VertexAttribute
        {
        };

        struct PushConstant
        {
            std::string  name;
            std::uint8_t offset;
            std::uint8_t size;
        };

        struct PushConstants
        {
            std::uint8_t                                          size;
            Container<PushConstant, Limits::gMaxNumPushConstants> variables;
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

        Container<VertexAttribute, Limits::gMaxNumVertexAttributes> vertexAttributes;
        PushConstants                                               pushConstants;
        Container<UniformBuffer, Limits::gMaxNumUniformBuffers>     uniformBuffers;
        Container<UniformBuffer, Limits::gMaxNumUniformBuffers>     storageBuffers;
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
