#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>
#include <VOG/Graphics/Typedefs.hpp>
#include <VOG/Graphics/Vulkan/Containers.hpp>
#include <VOG/Graphics/Vulkan/Limits.hpp>

#include <compare>
#include <memory>
#include <span>
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
     * #pushConstants sorted by BlockMember::offset ascending. #heapBlocks sorted by
     * HeapBlock::name; the members of a heap block are in declaration order.
     *
     * A stage input or output that is an interface block has one StageInOutAttribute per member,
     * named "Block.member": the name of the block, a dot, the name of the member. A struct
     * variable that is no block is named after the variable instead. A member has the location of
     * its own Location decoration, else the location after the member before it; the first member
     * has the location of the block. A member that is itself a struct, and an array of either, are
     * a ShaderError.
     *
     * A shader reaches its resources through the descriptor heap (`layout(descriptor_heap)`), by an
     * index it reads from the push constants. SPIRV-Cross lists none of them as a resource, so the
     * blocks are found from the instructions that reach them: a block that no instruction of the
     * stage reaches is not in the SPIR-V and not in #heapBlocks. Only blocks are reflected; a heap
     * array of images or samplers is not. Any atomic on a heap block, and any
     * length() of an unsized array in one, is a ShaderError, whatever else the stage reads of the
     * block: such an access is one the reflection cannot follow. A member that is a
     * buffer_reference is a ShaderError too. SPIRV-Cross itself overflows its stack in its
     * constructor when the stage dereferences such a pointer to a struct that points to itself
     * (measured with the pinned version, in a heap block: `head.value` with `Node head` is
     * enough); VOG code is not reached then.
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
             * Consecutive locations the input or output covers, from #location: the array sizes
             * multiplied, times one location per matrix column, times two for a 64-bit vector of
             * three or four components.
             */
            std::uint32_t locationCount = 1u;
        };

        /** A member of a push block or of a block that is read through the descriptor heap. */
        struct BlockMember
        {
            std::string   name;
            std::uint32_t offset = 0u;
            /** Bytes the member takes in its block, as declared: all elements of an array. */
            std::uint32_t size = 0u;
            /** Numeric type of a scalar, vector or matrix member; zero components for a struct. */
            BaseType type{};
            /** Elements of an array, all dimensions multiplied; 0 for no array or an unsized one.
             */
            std::uint32_t arraySize = 0u;
            /**
             * Bytes between two elements of the outermost dimension of an array; 0 for no array.
             * The inner dimensions of a multi-dimensional array have strides of their own, which
             * are not reflected.
             */
            std::uint32_t arrayStride = 0u;
            /** Bytes between the columns of a matrix; 0 for no matrix. */
            std::uint32_t matrixStride = 0u;
            bool          rowMajor     = false;
            /** Name of the struct type of a struct member (or of an array of them); else empty. */
            std::string structName;
            /** The members of a struct member, in declaration order. */
            std::vector<BlockMember> members;

            bool operator==(const BlockMember&) const = default;
        };

        enum class HeapBlockStorage : std::uint8_t
        {
            /** A `uniform` block. */
            eUniform,
            /** A `buffer` block. */
            eStorageBuffer,
        };

        /** A block that a stage reads through the descriptor heap. */
        struct HeapBlock
        {
            std::string      name;
            HeapBlockStorage storage = HeapBlockStorage::eUniform;
            /** Bytes the block takes: the end of its last member, as declared. */
            std::uint32_t size = 0u;
            /**
             * The `descriptor_stride` of the heap array that holds the block: the bytes between
             * two elements. 0 when the shader declares none (the stride is then the size of a
             * descriptor), or the stride is not a constant.
             */
            std::uint32_t strideDecoration = 0u;
            /** In declaration order. */
            std::vector<BlockMember> members;

            bool operator==(const HeapBlock&) const = default;
        };

        using StageAttributes = std::vector<StageInOutAttribute>;

        StageAttributes          inAttributes;
        StageAttributes          outAttributes;
        std::vector<BlockMember> pushConstants;
        std::vector<HeapBlock>   heapBlocks;
    };

    /** "vec3", "ivec4", "mat4", "float": how a shader declares a value of type @p type. */
    [[nodiscard]] static std::string describe(Reflection::BaseType type);

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
