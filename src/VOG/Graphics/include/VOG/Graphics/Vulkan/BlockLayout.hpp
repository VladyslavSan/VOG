#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace VOG::Graphics::Vulkan
{
/** The GLSL type a C++ block member type matches, with its column stride. */
struct BlockMemberType
{
    Shader::Reflection::BaseType type;
    /** sizeof a column for a matrix; 0 otherwise. */
    std::uint32_t matrixStride;
};

namespace Detail
{
template <class T>
concept HasLength = requires {
    { T::length() } -> std::convertible_to<std::size_t>;
};

template <class T>
concept BlockMatrix = HasLength<T> && requires { typename T::col_type; };

template <class T>
concept BlockVector = HasLength<T> && requires { typename T::value_type; };

template <class S>
concept BlockScalar = std::same_as<S, float> || std::same_as<S, double> ||
                      std::same_as<S, std::int32_t> || std::same_as<S, std::uint32_t>;

template <BlockScalar S>
consteval Shader::Reflection::BaseType::Type
scalarType()
{
    using Type = Shader::Reflection::BaseType::Type;

    if constexpr (std::same_as<S, float>)
    {
        return Type::eFloat;
    }
    else if constexpr (std::same_as<S, double>)
    {
        return Type::eDouble;
    }
    else if constexpr (std::same_as<S, std::int32_t>)
    {
        return Type::eInt;
    }
    else
    {
        return Type::eUInt;
    }
}
} // namespace Detail

/** The C++ types that blockMemberType() maps to a GLSL type. */
template <class M>
concept BlockMemberCompatible =
    Detail::BlockScalar<M> || Detail::BlockMatrix<M> || Detail::BlockVector<M>;

/**
 * The block member type that the C++ type @p M matches. float, double, std::int32_t and
 * std::uint32_t are scalars. A type with a `col_type` and a static `length()` is a matrix of
 * length() columns of col_type; that rule is tried first, because a matrix has the members of a
 * vector too. Otherwise a type with a `value_type` and a static `length()` is a vector of
 * length() components. glm::vec and glm::mat match by that structure, so Graphics needs no glm. A
 * glm quaternion matches as a vec4 in its storage order. The size of a vector or matrix must be
 * length() times the size of its element. A type that matches none of these, such as bool, fails
 * the BlockMemberCompatible constraint. A column of a glm::mat3 is 12 bytes and a GLSL one is 16,
 * which ShaderProgram::requireHeapBlock() reports as a matrix stride mismatch.
 */
template <BlockMemberCompatible M>
consteval BlockMemberType
blockMemberType()
{
    if constexpr (Detail::BlockScalar<M>)
    {
        return {.type         = {.type = Detail::scalarType<M>(), .components = 1u, .columns = 1u},
                .matrixStride = 0u};
    }
    else if constexpr (Detail::BlockMatrix<M>)
    {
        using Column = M::col_type;
        static_assert(Detail::BlockVector<Column> &&
                          Detail::BlockScalar<typename Column::value_type>,
                      "The column of a matrix must be a vector of float, double, int or uint.");
        static_assert(sizeof(M) == M::length() * sizeof(Column),
                      "A matrix must be its columns and nothing else.");

        return {.type         = {.type       = Detail::scalarType<typename Column::value_type>(),
                                 .components = static_cast<std::uint8_t>(Column::length()),
                                 .columns    = static_cast<std::uint8_t>(M::length())},
                .matrixStride = sizeof(Column)};
    }
    else
    {
        static_assert(Detail::BlockVector<M> && Detail::BlockScalar<typename M::value_type>,
                      "A block member is a float, double, int or uint scalar, vector or matrix.");
        static_assert(sizeof(M) == M::length() * sizeof(typename M::value_type),
                      "A vector must be its components and nothing else.");

        return {.type         = {.type       = Detail::scalarType<typename M::value_type>(),
                                 .components = static_cast<std::uint8_t>(M::length()),
                                 .columns    = 1u},
                .matrixStride = 0u};
    }
}

/**
 * The members of a C++ struct, to check a reflected block against (ShaderProgram::requirePushBlock
 * and requireHeapBlock). The names are the GLSL names of the members.
 */
class BlockLayout
{
public:
    struct Member
    {
        std::string     name;
        std::uint32_t   offset;
        BlockMemberType type;
    };

    /** One member of T: its GLSL name and its pointer, as in {"tint", &T::tint}. */
    template <class T>
    struct Entry
    {
        template <BlockMemberCompatible M>
        // Not explicit: an Entry is written as a braced pair in the list that of() takes.
        // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions)
        Entry(std::string_view name, M T::* pointer)
            : member{.name   = std::string{name},
                     .offset = offsetOf(pointer),
                     .type   = blockMemberType<M>()}
        {
        }

        Member member;

    private:
        /**
         * Where the member is in T, read from the addresses of a value-initialized sample: no
         * null pointer is dereferenced and no object is read outside its lifetime.
         */
        template <class M>
        static std::uint32_t
        offsetOf(M T::* pointer)
        {
            static const T kSample{};

            return static_cast<std::uint32_t>(
                std::bit_cast<std::uintptr_t>(std::addressof(kSample.*pointer)) -
                std::bit_cast<std::uintptr_t>(std::addressof(kSample)));
        }
    };

    /**
     * The layout of @p entries, which name members of T once each, in any order. T is a standard
     * layout type, so its members are in declaration order with no base or vtable, and trivially
     * copyable, because its bytes are uploaded or pushed. Not constexpr: keep a layout in a
     * function-local static.
     */
    template <class T>
    [[nodiscard]] static BlockLayout
    of(std::initializer_list<Entry<T>> entries)
    {
        static_assert(std::is_standard_layout_v<T>, "A block struct must be standard layout.");
        static_assert(std::is_trivially_copyable_v<T>,
                      "A block struct must be trivially copyable.");
        static_assert(std::is_default_constructible_v<T>,
                      "A block struct must be default constructible.");

        std::vector<Member> members;
        members.reserve(entries.size());
        for (const Entry<T>& entry : entries)
        {
            members.push_back(entry.member);
        }
        std::ranges::sort(members, std::ranges::less{}, &Member::offset);

        return BlockLayout{std::move(members)};
    }

    /** Sorted by offset. */
    const std::vector<Member> members;

private:
    explicit BlockLayout(std::vector<Member> sorted)
        : members{std::move(sorted)}
    {
    }
};
} // namespace VOG::Graphics::Vulkan
