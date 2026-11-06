#pragma once

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <string>
#include <string_view>

namespace VOG::Tests
{
/**
 * A shader directory of its own, in the temporary directory, removed with the object. It starts as
 * a copy of the shaders of the engine (which brings the include VOG/Draw.glsl), plus the test
 * shaders of Test/resources/shaders, and a test adds the shaders it needs.
 */
class ShaderDirectory
{
public:
    ShaderDirectory()
        : mPath{std::filesystem::path{testing::TempDir()} /
                std::format("vog-shaders-{:016x}", std::random_device{}())}
    {
        std::filesystem::create_directories(mPath);
        const auto options = std::filesystem::copy_options::recursive;
        std::filesystem::copy(VOG_ENGINE_SHADER_PATH, mPath, options);
        std::filesystem::copy(VOG_TEST_SHADER_SOURCE_PATH, mPath, options);
    }

    ~ShaderDirectory() { std::filesystem::remove_all(mPath); }

    ShaderDirectory(const ShaderDirectory&)            = delete;
    ShaderDirectory& operator=(const ShaderDirectory&) = delete;

    /** Adds the shader @p name: `name.vert.glsl` and `name.frag.glsl`. */
    void
    add(std::string_view name, std::string_view vertexSource, std::string_view fragmentSource)
    {
        std::ofstream{mPath / (std::string{name} + ".vert.glsl")} << vertexSource;
        std::ofstream{mPath / (std::string{name} + ".frag.glsl")} << fragmentSource;
    }

    const std::filesystem::path&
    path() const
    {
        return mPath;
    }

private:
    std::filesystem::path mPath;
};

/** The engine's shaders and the test shaders, one directory for the whole test run. */
inline const std::filesystem::path&
sharedTestShaderPath()
{
    static const ShaderDirectory kDirectory;

    return kDirectory.path();
}
} // namespace VOG::Tests
