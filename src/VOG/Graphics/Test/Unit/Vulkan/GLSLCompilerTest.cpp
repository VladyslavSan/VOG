#include <VOG/Graphics/Vulkan/GLSLCompiler.hpp>

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
namespace fs = std::filesystem;

constexpr auto kStage = Shader::ShadingStage::eFragment;

/** A fragment shader that has the include directive @p directive, which brings in `kColor`. */
std::string
shaderIncluding(std::string_view directive)
{
    return std::format(R"(#version 450
#extension GL_GOOGLE_include_directive : require
{}
layout(location = 0) out vec4 outColor;
void main() {{ outColor = kColor; }}
)",
                       directive);
}

constexpr std::string_view kColorHeader = "const vec4 kColor = vec4(1.0, 0.0, 0.0, 1.0);\n";

/** A scratch directory for the include files of one test, removed with the fixture. */
class GLSLCompiler : public testing::Test
{
protected:
    void
    SetUp() override
    {
        mRoot = fs::path{testing::TempDir()} / "vog-glsl-compiler" /
                testing::UnitTest::GetInstance()->current_test_info()->name();
        fs::remove_all(mRoot);
        fs::create_directories(mRoot);
    }

    void
    TearDown() override
    {
        fs::remove_all(mRoot);
    }

    /** Writes @p text to @p relative under the scratch directory, and returns the path. */
    fs::path
    write(const fs::path& relative, std::string_view text) const
    {
        const fs::path path = mRoot / relative;
        fs::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << text;

        return path;
    }

    /** The error of compiling @p source, empty if it compiles. */
    static std::string
    errorOf(const std::string& source, std::span<const fs::path> directories)
    {
        try
        {
            EXPECT_FALSE(compileGLSL(kStage, source, directories).empty());
        }
        catch (const Shader::CompilationError& error)
        {
            return error.what();
        }

        return {};
    }

    fs::path mRoot;
};
} // namespace

/**
 * An include is looked up in the include directories in order, with either kind of quoting; a
 * header found in the second directory compiles, and when both directories have it the first wins.
 */
TEST_F(GLSLCompiler, includeIsFoundInTheDirectoriesInOrder)
{
    write("first/Other.glsl", "this is not GLSL and nothing includes it\n");
    write("second/Color.glsl", kColorHeader);
    const std::array directories{mRoot / "first", mRoot / "second"};

    EXPECT_EQ(errorOf(shaderIncluding("#include \"Color.glsl\""), directories), "");
    EXPECT_EQ(errorOf(shaderIncluding("#include <Color.glsl>"), directories), "");

    // The first directory wins: its header is the one that fails to compile.
    write("first/Color.glsl", "this is not GLSL\n");
    EXPECT_NE(errorOf(shaderIncluding("#include \"Color.glsl\""), directories), "");
}

/**
 * A header that is not there is an error that names it, not a crash: glslang hands a null result
 * back to the includer, which must accept it. With no directories every include fails.
 */
TEST_F(GLSLCompiler, missingIncludeThrowsAndNamesTheHeader)
{
    write("dir/Other.glsl", kColorHeader);
    const std::array directories{mRoot / "dir"};

    const std::string missing = errorOf(shaderIncluding("#include \"Color.glsl\""), directories);
    EXPECT_NE(missing.find("Color.glsl"), std::string::npos) << missing;

    const std::string none = errorOf(shaderIncluding("#include \"Other.glsl\""), {});
    EXPECT_NE(none.find("Other.glsl"), std::string::npos) << none;
}

/**
 * An include is resolved against the directories only, never against the file that includes: a
 * header next to its includer is not found unless its directory is one of them. A name with a root
 * is not resolved, even if the file exists, and neither is one that climbs out of the directory
 * with "..", though a ".." that stays inside it is.
 */
TEST_F(GLSLCompiler, includeIsNotResolvedRelativeToTheIncludingFile)
{
    write("dir/sub/Outer.glsl", "#include \"Color.glsl\"\n");
    const fs::path    color  = write("dir/sub/Color.glsl", kColorHeader);
    const std::string source = shaderIncluding("#include \"sub/Outer.glsl\"");

    const std::array  onlyRoot{mRoot / "dir"};
    const std::string relative = errorOf(source, onlyRoot);
    EXPECT_NE(relative.find("Color.glsl"), std::string::npos) << relative;

    const std::array both{mRoot / "dir", mRoot / "dir" / "sub"};
    EXPECT_EQ(errorOf(source, both), "");

    const std::string absolute =
        errorOf(shaderIncluding(std::format("#include \"{}\"", color.generic_string())), both);
    EXPECT_NE(absolute, "");

    // A header beside the directory, reached by climbing out of it.
    write("Outside.glsl", kColorHeader);
    const std::array inside{mRoot / "dir"};
    for (const auto* name : {"../Outside.glsl", "sub/../../Outside.glsl", "./../Outside.glsl"})
    {
        const std::string climbing =
            errorOf(shaderIncluding(std::format("#include \"{}\"", name)), inside);
        EXPECT_NE(climbing.find("Outside.glsl"), std::string::npos) << name << ": " << climbing;
    }
    EXPECT_EQ(errorOf(shaderIncluding("#include \"sub/../sub/Color.glsl\""), inside), "");
}

/**
 * A file that includes itself, with no include guard, ends in an error instead of recursing without
 * end, and so does a chain of distinct files deeper than gMaxIncludeDepth; a chain up to that
 * depth compiles.
 */
TEST_F(GLSLCompiler, includeChainDeeperThanTheCapStopsWithAnError)
{
    write("dir/Loop.glsl", "#include \"Loop.glsl\"\n");
    const std::array directories{mRoot / "dir"};

    const std::string loop = errorOf(shaderIncluding("#include \"Loop.glsl\""), directories);
    EXPECT_NE(loop.find("Loop.glsl"), std::string::npos) << loop;

    // File i includes file i + 1; the last one defines the color.
    const auto chain = [this](std::size_t length)
    {
        for (std::size_t i = 0u; i + 1u < length; ++i)
        {
            write(std::format("dir/Chain{}.glsl", i),
                  std::format("#include \"Chain{}.glsl\"\n", i + 1u));
        }
        write(std::format("dir/Chain{}.glsl", length - 1u), kColorHeader);
    };
    chain(gMaxIncludeDepth);
    EXPECT_EQ(errorOf(shaderIncluding("#include \"Chain0.glsl\""), directories), "");

    chain(gMaxIncludeDepth + 1u);
    const std::string deep = errorOf(shaderIncluding("#include \"Chain0.glsl\""), directories);
    EXPECT_NE(deep.find("Chain"), std::string::npos) << deep;
}
} // namespace VOG::Tests
