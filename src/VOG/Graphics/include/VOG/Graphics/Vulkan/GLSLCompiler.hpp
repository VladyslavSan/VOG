#pragma once

#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace VOG::Graphics::Vulkan
{
/** Deepest chain of `#include` directives that compileGLSL() follows. */
inline constexpr std::size_t gMaxIncludeDepth = 32u;

/**
 * Compiles @p glslCode, the source of one @p stage, to SPIR-V.
 *
 * An `#include "name"` or `#include <name>` needs `#extension GL_GOOGLE_include_directive :
 * require` in the source. It is resolved against @p includeDirectories only: the directories are
 * tried in order for `directory / name`, never relative to the file that includes. A name with a
 * root (an absolute path) is not resolved, and neither is one that starts with ".." once "." and
 * ".." are folded, which would leave the directory. With no directories every include fails. A
 * chain of includes deeper than gMaxIncludeDepth fails, so a file that includes itself without a
 * guard ends in an error.
 *
 * A `bool` member of a block that the shader reads through the descriptor heap crashes glslang in
 * this function, which nothing here can catch: `uniform Foo { bool b; vec4 v; } gFoos[];` with
 * `b` read ends the process with SIGSEGV (measured with the pinned glslang). A `bvec2` member, and
 * a `bool` inside a struct member, crash the same way when read; the same block with a set and a
 * binding compiles, and so does a `bool` member that the shader never reads. Declare such a member
 * as `uint`.
 *
 * @throws Shader::CompilationError if the source does not compile or link, or an include cannot be
 *         resolved. The message holds the compiler's log.
 */
[[nodiscard]] std::vector<std::uint32_t>
compileGLSL(Shader::ShadingStage                   stage,
            const std::string&                     glslCode,
            std::span<const std::filesystem::path> includeDirectories = {});
} // namespace VOG::Graphics::Vulkan
