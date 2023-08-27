#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <boost/container/static_vector.hpp>

#include <compare>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace VOG::Graphics::Vulkan
{
class Device;

class Shader
{
public:
    template <class T, std::size_t N>
    using Container = boost::container::static_vector<T, N>;

    class CompilationError : public std::runtime_error
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

            std::strong_ordering
            operator<=>(const StructMember& pushConstant) const
            {
                return offset <=> pushConstant.offset;
            }
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

            auto operator<=>(const ResourceLocation&) const = default;
        };

        struct UniformBuffer
        {
            ResourceLocation          location;
            std::size_t               size;
            std::vector<StructMember> members;
        };

        using StageAttributes = Container<StageInOutAttribute, Limits::gMaxNumStageAttributes>;

        StageAttributes                                         inAttributes;
        StageAttributes                                         outAttributes;
        PushConstants                                           pushConstants;
        Container<UniformBuffer, Limits::gMaxNumUniformBuffers> uniformBuffers;
    };

    using DescriptorSetBindings = std::vector<vk::DescriptorSetLayoutBinding>;
    using DescriptorMap         = std::unordered_map<std::uint8_t, vk::DescriptorSetLayout>;

    static std::shared_ptr<Shader>
    create(const Device& device, ShadingStage stage, const std::string& source);

    /**
     * Utility function used for validation whether @p provided vertex format is compatible with
     * @p shaderVertexFormat.
     *
     * @param shaderVertexFormat Vertex attribute format in the shader.
     * @param provided Vertex attribute bound from vertex buffer.
     * @return true if @p provided can be bound correctly into @p shaderVertexFormat.
     */
    static bool vertexFormatCompatible(Shader::Reflection::AttributeFormat shaderVertexFormat,
                                       vk::Format                          provided);

    /**
     * Build reflection info out of spirv binary.
     *
     * @param binary Compiled spirv binary.
     * @return reflection data of @p binary shader.
     */
    static Reflection reflect(const std::vector<std::uint32_t>& binary);

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

    const ShadingStage               stage;
    const std::vector<std::uint32_t> binary;
    const vk::raii::ShaderModule     module;
    const Reflection                 reflection;
};
using ShaderPtr = std::shared_ptr<Shader>;
} // namespace VOG::Graphics::Vulkan
