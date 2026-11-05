#include "VOG/Engine/Renderer/DrawCheck.hpp"

#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <cstddef>
#include <format>
#include <string>

namespace VOG::Engine::Detail
{
namespace
{
std::string
describeLayout(const VertexLayout& layout)
{
    std::string result;
    for (const VertexAttribute& attribute : layout.inputs())
    {
        result += std::format(
            "{}{} @{}", result.empty() ? "" : ", ", describe(attribute.format), attribute.offset);
    }

    return result;
}
} // namespace

std::optional<MissingInput>
DrawCheck::evaluate(const VertexLayout& layout, const ShaderInfo& shader)
{
    using Graphics::Vulkan::Shader;

    const auto inputs = layout.inputs();
    for (const VertexInput& input : shader.inputs)
    {
        for (std::uint32_t location = input.location;
             location < input.location + input.locationCount;
             ++location)
        {
            const bool provided = location < inputs.size();
            if (provided &&
                Shader::vertexFormatCompatible(input.type, toVk(inputs[location].format)))
            {
                continue;
            }

            return MissingInput{.kind     = provided ? MissingInput::Kind::eWrongType
                                                     : MissingInput::Kind::eNoAttribute,
                                .location = location,
                                .type     = Shader::describe(input.type),
                                .name     = input.name};
        }
    }

    return std::nullopt;
}

std::string
DrawCheck::describe(const MeshData& mesh, const ShaderInfo& shader, const MissingInput& missing)
{
    std::string subject = std::format("mesh \"{}\" ({})", mesh.name, describeLayout(mesh.layout));

    switch (missing.kind)
    {
    case MissingInput::Kind::eNoAttribute:
        return std::format("{} does not provide location {} ({} {}) of shader \"{}\"",
                           subject,
                           missing.location,
                           missing.type,
                           missing.name,
                           shader.name);
    case MissingInput::Kind::eWrongType:
        return std::format(
            "{} provides {} at location {}, which cannot feed ({} {}) of shader \"{}\"",
            subject,
            Engine::describe(mesh.layout.inputs()[missing.location].format),
            missing.location,
            missing.type,
            missing.name,
            shader.name);
    }

    return subject;
}

void
DrawCheck::require(const MeshData& mesh, const ShaderInfo& shader, std::string_view debugName)
{
    const auto [verdict, added] = mVerdicts.try_emplace({mesh.layoutId, shader.programId});
    if (added)
    {
        verdict->second = evaluate(mesh.layout, shader);
    }

    if (verdict->second)
    {
        const std::string sentence = describe(mesh, shader, *verdict->second);

        throw DrawError{debugName.empty() ? sentence : std::format("{}: {}", debugName, sentence)};
    }
}
} // namespace VOG::Engine::Detail
