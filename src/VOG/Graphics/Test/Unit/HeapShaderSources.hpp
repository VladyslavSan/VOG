#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <format>
#include <string>
#include <string_view>

namespace VOG::Tests
{
/** `gMaterials[...]` of a block that the Material stage reads: stride, storage and members. */
inline std::string
materialBlock(std::string_view storage, std::string_view stride, std::string_view members)
{
    return std::format("layout(descriptor_heap{}) {} Material {{ {} }} gMaterials[];",
                       stride.empty() ? std::string{} : std::format(", {}", stride),
                       storage,
                       members);
}

/**
 * The source of a @p stage whose only work is to read @p read, a vec4 expression, which may use
 * the push constant `pushData.index` and the heap blocks that @p declarations declares. Every
 * stage has the push block `PushData { uint index; }`.
 */
inline std::string
heapShader(VOG::Graphics::Vulkan::Shader::ShadingStage stage,
           std::string_view                            declarations,
           std::string_view                            read)
{
    const std::string prelude = std::format(
        R"(#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
{}
layout(push_constant) uniform PushData {{ uint index; }} pushData;
)",
        declarations);

    if (stage == VOG::Graphics::Vulkan::Shader::ShadingStage::eVertex)
    {
        return std::format("{}void main() {{ gl_Position = {}; }}\n", prelude, read);
    }

    return std::format(
        "{}layout(location = 0) out vec4 outColor;\nvoid main() {{ outColor = {}; }}\n",
        prelude,
        read);
}
} // namespace VOG::Tests
