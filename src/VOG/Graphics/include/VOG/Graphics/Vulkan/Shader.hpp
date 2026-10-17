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
VOG_DECLARE_PTR(ShaderProgram);

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
     * #inAttributes and #outAttributes sorted by StageInOutAttribute::location ascending.
     * #pushConstants.variables sorted by StructMember::offset ascending.
     *
     * Descriptors are not reflected: a shader reaches its resources through the descriptor heap
     * (`layout(descriptor_heap)`), by an index it reads from the push constants.
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
            /**
             * Consecutive locations the input covers, from #location: the array sizes multiplied,
             * times one location per matrix column.
             */
            std::uint32_t locationCount = 1u;
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

        using StageAttributes = std::vector<StageInOutAttribute>;

        StageAttributes inAttributes;
        StageAttributes outAttributes;
        PushConstants   pushConstants;
    };

    /**
     * Utility function used for validation whether @p provided vertex format is compatible with
     * @p shaderVertexFormat. Only the numeric type is compared (float, signed or unsigned
     * integer); the component counts may differ. 64-bit types are never compatible: VOG binds
     * no 64-bit vertex formats, and they take two locations.
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
