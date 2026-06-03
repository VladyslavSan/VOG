#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <compare>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace VOG::Graphics::Vulkan
{
VOG_DECLARE_PTR(Device);
class ShaderProgram;

class Shader
{
    friend class ShaderProgram;

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
        struct BaseType
        {
            enum class Type : std::uint8_t
            {
                eInt = 0,
                eUInt,
                eFloat,
                eDouble,
            };

            std::strong_ordering operator<=>(const BaseType&) const = default;

            Type         type       : 4;
            std::uint8_t components : 4;
            std::uint8_t columns    : 4;
        };

        struct StageInOutAttribute
        {
            std::uint8_t location;
            std::string  name;
            BaseType     format;
        };

        struct StructMember
        {
            std::uint16_t offset;
            std::uint16_t size;
            std::string   name;
            BaseType      type;

            std::strong_ordering operator<=>(const StructMember&) const = default;
        };

        struct PushConstants
        {
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

            std::strong_ordering operator<=>(const UniformBuffer&) const noexcept = default;
        };

        using StageAttributes = std::vector<StageInOutAttribute>;

        StageAttributes            inAttributes;
        StageAttributes            outAttributes;
        PushConstants              pushConstants;
        std::vector<UniformBuffer> uniformBuffers;
    };

    /**
     * Utility function used for validation whether @p provided vertex format is compatible with
     * @p shaderVertexFormat.
     *
     * @param shaderVertexFormat Vertex attribute format in the shader.
     * @param provided Vertex attribute bound from vertex buffer.
     *
     * @return true if @p provided can be bound correctly into @p shaderVertexFormat.
     */
    static bool vertexFormatCompatible(Shader::Reflection::BaseType shaderVertexFormat,
                                       vk::Format                   provided);

private:
    friend class Device;

    Shader(DevicePtr device, ShadingStage stage, std::span<const std::uint32_t> shaderBinary);

    const DevicePtr device;

public:
    const vk::ShaderStageFlagBits stage;
    const vk::raii::ShaderModule  module;
    const Reflection              reflection;
};
using ShaderPtr = std::shared_ptr<Shader>;
} // namespace VOG::Graphics::Vulkan
