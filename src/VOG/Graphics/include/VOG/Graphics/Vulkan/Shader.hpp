#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <compare>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);

class Shader
{
public:
    class CompilationError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    class ShaderError : public std::runtime_error
    {
        using std::runtime_error::runtime_error;
    };

    enum class ShadingStage : std::uint8_t
    {
        eVertex   = 0,
        eFragment = 1
    };

    /**
     * Structure that holds shader reflection information.
     *
     * #vertexAttributes sorted by VertexAttribute::location field ascending.
     * #pushConstants sorted by PushConstant::offset ascending.
     * #uniformBuffers and #storageBuffers sorted by UniformBuffer::location.
     */
    struct Reflection
    {
        struct AttributeFormat
        {
            enum class Type : std::uint8_t
            {
                eInt = 0,
                eUInt,
                eFloat,
                eDouble,
            };

            Type         type       : 4;
            std::uint8_t components : 2;
        };

        struct StageInOutAttribute
        {
            std::uint8_t    location;
            std::string     name;
            AttributeFormat format;
        };

        struct StructMember
        {
            std::uint16_t offset;
            std::uint16_t size;
            std::string   name;

            std::strong_ordering operator<=>(const StructMember& pushConstant) const = default;
        };

        struct PushConstants
        {
            std::uint8_t              size;
            std::vector<StructMember> variables;
        };

        struct ResourceLocation
        {
            std::uint32_t set;
            std::uint32_t binding;

            std::strong_ordering operator<=>(const ResourceLocation&) const noexcept = default;
        };

        struct UniformBuffer
        {
            ResourceLocation          location;
            std::size_t               size;
            std::vector<StructMember> members;

            std::strong_ordering operator<=>(const UniformBuffer& rhs) const noexcept = default;
        };

        using StageAttributes =
            StaticVectorStrict<StageInOutAttribute, Limits::gMaxNumStageAttributes>;

        StageAttributes                                                  inAttributes;
        StageAttributes                                                  outAttributes;
        PushConstants                                                    pushConstants;
        StaticVectorStrict<UniformBuffer, Limits::gMaxNumUniformBuffers> uniformBuffers;
    };

    /**
     * Helper create method.
     *
     * @param device Vulkan device to construct the shader module.
     * @param stage Shading stage.
     * @param shaderBinary SPIR-V shader binary code.
     *
     * @return shared pointer to Shader instance.
     */
    static std::shared_ptr<Shader>
    create(DevicePtr device, ShadingStage stage, const std::string& source);

    /**
     * Utility function used for validation whether @p provided vertex format is compatible with
     * @p shaderVertexFormat.
     *
     * @param shaderVertexFormat Vertex attribute format in the shader.
     * @param provided Vertex attribute bound from vertex buffer.
     *
     * @return true if @p provided can be bound correctly into @p shaderVertexFormat.
     */
    static bool vertexFormatCompatible(Shader::Reflection::AttributeFormat shaderVertexFormat,
                                       vk::Format                          provided);

    /**
     * Construct from glsl shader code. Very expensive as it involves glsl to spirv compilation.
     *
     * @param device Vulkan device to construct the shader module.
     * @param stage Shading stage.
     * @param shaderBinary SPIR-V shader binary code.
     */
    Shader(DevicePtr device, ShadingStage stage, const std::string& glslCode);

    /**
     * Construct from SPIR-V shader binary.
     *
     * @param device Vulkan device to construct the shader module.
     * @param stage Shading stage.
     * @param shaderBinary SPIR-V shader binary code.
     */
    Shader(DevicePtr device, ShadingStage stage, std::vector<std::uint32_t> shaderBinary);

    const DevicePtr                  device;
    const vk::ShaderStageFlagBits    stage;
    const std::vector<std::uint32_t> binary;
    const vk::raii::ShaderModule     module;
    const Reflection                 reflection;
};
using ShaderPtr = std::shared_ptr<Shader>;
} // namespace VOG::Graphics::Vulkan
