#include "VOG/Engine/Renderer/MaterialDefinition.hpp"

#include "VOG/Engine/Renderer/DrawPushData.hpp"
#include "VOG/Engine/Renderer/ResourceFactory.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <algorithm>
#include <concepts>
#include <format>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

namespace VOG::Engine
{
namespace Detail
{
std::string_view
describe(ParameterType type)
{
    using enum ParameterType;

    switch (type)
    {
    case eFloat:
        return "float";
    case eFloat2:
        return "vec2";
    case eFloat3:
        return "vec3";
    case eFloat4:
        return "vec4";
    case eInt:
        return "int";
    case eInt2:
        return "ivec2";
    case eInt3:
        return "ivec3";
    case eInt4:
        return "ivec4";
    case eUInt:
        return "uint";
    case eUInt2:
        return "uvec2";
    case eUInt3:
        return "uvec3";
    case eUInt4:
        return "uvec4";
    case eFloat3x3:
        return "mat3";
    case eFloat4x4:
        return "mat4";
    case eTexture:
        return "TextureRef";
    }

    return {};
}
} // namespace Detail

std::span<const MaterialParameter>
Material::parameters() const noexcept
{
    return mData ? std::span<const MaterialParameter>{mData->parameters}
                 : std::span<const MaterialParameter>{};
}

std::span<const ShaderParameter>
Shader::parameters() const noexcept
{
    return mData ? std::span<const ShaderParameter>{mData->parameters}
                 : std::span<const ShaderParameter>{};
}

namespace
{
using Detail::MaterialMember;
using Detail::ParameterType;

/** "a vec4", "an ivec2": the article that goes with the GLSL name of a type. */
std::string
withArticle(std::string_view type)
{
    return std::format("{} {}", type.starts_with('i') ? "an" : "a", type);
}

ParameterType
typeOf(const MaterialValue& value)
{
    return static_cast<ParameterType>(value.index());
}

/** "tint (vec4), albedo (TextureRef)". */
std::string
listMembers(std::span<const MaterialMember> members)
{
    std::string text;
    for (const MaterialMember& member : members)
    {
        text +=
            std::format("{}{} ({})", text.empty() ? "" : ", ", member.name, describe(member.type));
    }

    return text;
}

/** Whether two types differ only in the kind of their numbers: a float and an int, say. */
bool
differInNumberKindOnly(ParameterType expected, ParameterType given)
{
    using enum ParameterType;

    const auto shapeOf = [](ParameterType type)
    {
        switch (type)
        {
        case eFloat:
        case eInt:
        case eUInt:
            return 1;
        case eFloat2:
        case eInt2:
        case eUInt2:
            return 2;
        case eFloat3:
        case eInt3:
        case eUInt3:
            return 3;
        case eFloat4:
        case eInt4:
        case eUInt4:
            return 4;
        default:
            return 0;
        }
    };

    return expected != given && shapeOf(expected) != 0 && shapeOf(expected) == shapeOf(given);
}

/** Writes @p value at @p offset of @p block. */
template <class Value>
void
writeBytes(std::span<std::byte> block, std::uint32_t offset, const Value& value)
{
    std::ranges::copy(std::as_bytes(std::span{&value, 1u}), block.subspan(offset).begin());
}
} // namespace

void
ResourceFactory::writeMaterialValue(std::span<std::byte>  block,
                                    const MaterialMember& member,
                                    const MaterialValue&  value)
{
    std::visit(
        [&](const auto& held)
        {
            using Held = std::decay_t<decltype(held)>;

            if constexpr (std::same_as<Held, Texture>)
            {
                writeBytes(
                    block,
                    member.offset,
                    TextureRef{.index        = held.mData->texture->heapIndex(),
                               .samplerIndex = static_cast<std::uint32_t>(held.mData->filter)});
            }
            else if constexpr (std::same_as<Held, Math::Matrix3x3f> ||
                               std::same_as<Held, Math::Matrix4x4f>)
            {
                // The columns of a matrix are matrixStride bytes apart in the block, and a mat3
                // column of 12 bytes leaves a gap.
                for (std::uint32_t column = 0u; column < Held::length(); ++column)
                {
                    writeBytes(block, member.offset + (column * member.matrixStride), held[column]);
                }
            }
            else
            {
                writeBytes(block, member.offset, held);
            }
        },
        value);
}

std::vector<std::byte>
ResourceFactory::packMaterialBlock(const Detail::ShaderInfo&          shader,
                                   std::span<const MaterialParameter> parameters,
                                   std::string_view                   context)
{
    const auto fail = [&context](const std::string& message) -> std::invalid_argument
    {
        return std::invalid_argument{std::format("{}{}", context, message)};
    };

    // Per member of the block, the value that was given for it.
    std::vector<const MaterialValue*> given(shader.members.size(), nullptr);

    for (const MaterialParameter& parameter : parameters)
    {
        const auto member =
            std::ranges::find(shader.members, parameter.name, &MaterialMember::name);
        if (member == shader.members.end())
        {
            if (shader.members.empty())
            {
                throw fail(
                    std::format("Shader \"{}\" reads no Material block (a block no stage "
                                "reads is compiled out), so it takes no parameters; got "
                                "\"{}\".",
                                shader.name,
                                parameter.name));
            }
            throw fail(
                std::format("Shader \"{}\" has no material parameter \"{}\"; its parameters "
                            "are {}.",
                            shader.name,
                            parameter.name,
                            listMembers(shader.members)));
        }

        const auto slot = static_cast<std::size_t>(member - shader.members.begin());
        if (given[slot] != nullptr)
        {
            throw fail(std::format("Material parameter \"{}\" is set twice.", parameter.name));
        }

        const ParameterType type = typeOf(parameter.value);
        if (type != member->type)
        {
            std::string message =
                std::format("Material parameter \"{}\" of shader \"{}\" is {}; got {}.",
                            parameter.name,
                            shader.name,
                            withArticle(describe(member->type)),
                            withArticle(describe(type)));
            if (differInNumberKindOnly(member->type, type))
            {
                message += " Write 1.0f for a float, 1u for a uint.";
            }
            throw fail(message);
        }

        if (type == ParameterType::eTexture && !std::get<Texture>(parameter.value))
        {
            throw fail(std::format("Material parameter \"{}\" of shader \"{}\" is a null texture.",
                                   parameter.name,
                                   shader.name));
        }

        given[slot] = &parameter.value;
    }

    std::vector<MaterialMember> missing;
    for (const auto& [member, value] : std::views::zip(shader.members, given))
    {
        if (value == nullptr)
        {
            missing.push_back(member);
        }
    }
    if (!missing.empty())
    {
        throw fail(
            std::format("Shader \"{}\" needs values for {}.", shader.name, listMembers(missing)));
    }

    std::vector<std::byte> block(shader.blockSize);
    for (const auto& [member, value] : std::views::zip(shader.members, given))
    {
        writeMaterialValue(block, member, *value);
    }

    return block;
}
} // namespace VOG::Engine
