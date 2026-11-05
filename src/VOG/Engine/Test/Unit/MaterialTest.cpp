#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include "VOG/Engine/Renderer/TextureDefinition.hpp"
#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/DeviceRequirements.hpp>
#include <VOG/Graphics/Vulkan/HeapBuffer.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Graphics/Vulkan/Texture.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "HandleTestAccess.hpp"
#include "TestShaders.hpp"

using namespace VOG::Engine;

namespace VOG::Tests
{
namespace
{
/** A vertex shader that every test shader can have: the position of the origin, through the Frame
 *  block and the model matrix of the include. */
constexpr std::string_view gVertexSource = R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "VOG/Draw.glsl"
void main() { gl_Position = gFrame.viewProjection * VOG_MODEL * vec4(0.0, 0.0, 0.0, 1.0); }
)";

/** A fragment shader of the include with @p declarations, whose color is @p read, a vec4. */
std::string
fragmentSource(std::string_view declarations, std::string_view read)
{
    return std::format(R"(#version 450
#extension GL_GOOGLE_include_directive : require
#include "VOG/Draw.glsl"
{}
layout(location = 0) out vec4 outColor;
void main() {{ outColor = {}; }}
)",
                       declarations,
                       read);
}

/** A fragment shader without the include: the declarations are its own. Index 0 is the only one
 *  it reads, so it needs no push constant. */
std::string
standaloneFragmentSource(std::string_view declarations, std::string_view read)
{
    return std::format(R"(#version 450
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : require
{}
layout(location = 0) out vec4 outColor;
void main() {{ outColor = {}; }}
)",
                       declarations,
                       read);
}

constexpr std::uint32_t gRendererSize = 16u;

constexpr std::string_view gPlainVertexSource = R"(#version 450
void main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }
)";

/** The texts of @p exception and of every exception nested in it, one per line. */
std::string
chainOf(const std::exception& exception)
{
    std::string text = std::format("{}\n", exception.what());
    try
    {
        std::rethrow_if_nested(exception);
    }
    catch (const std::exception& nested)
    {
        text += chainOf(nested);
    }
    catch (...) // NOLINT(bugprone-empty-catch)
    {
    }

    return text;
}

bool
contains(const std::string& text, std::string_view part)
{
    return text.contains(part);
}

class MaterialTest : public testing::Test
{
protected:
    /** What loading the shaders of @p directory throws, as text; empty if they load. */
    std::string
    loadError(const ShaderDirectory& directory)
    {
        try
        {
            const ShaderLibrary library{mDevice, directory.path()};
        }
        catch (const std::exception& exception)
        {
            return chainOf(exception);
        }

        return {};
    }

    /** The error of the shader "Case" of a directory made of the engine's shaders and it. */
    std::string
    loadErrorOf(std::string_view vertexSource, std::string_view fragmentSource)
    {
        ShaderDirectory directory;
        directory.add("Case", vertexSource, fragmentSource);

        return loadError(directory);
    }

    static std::unique_ptr<Renderer>
    makeRenderer(const std::filesystem::path& shaderPath)
    {
        return std::make_unique<Renderer>(
            Common::SurfaceHandle{.platform = Common::SurfaceHandle::Platform::eHeadless},
            Renderer::Config{.appName          = "VOG Material Test",
                             .engineName       = "Test",
                             .extensions       = {"VK_EXT_headless_surface"},
                             .shaderSourcePath = shaderPath,
                             .extent = {.width = gRendererSize, .height = gRendererSize}});
    }

    /** A 2x2 texture of black pixels. */
    static Texture
    makeTexture(Renderer& renderer, Filter filter = Filter::eLinear)
    {
        const std::vector<std::byte> pixels(std::size_t{2u} * 2u * 4u);

        return renderer.createTexture(
            {.pixels = pixels, .width = 2u, .height = 2u, .filter = filter});
    }

    Graphics::Vulkan::DevicePtr mDevice =
        Graphics::Vulkan::Instance::create({.appName = "VOG Material Test", .engineName = "Test"})
            ->makeDevice();
};

/** Skips the test where the Renderer cannot be made. */
#define VOG_REQUIRE_HEADLESS()                                                                     \
    do                                                                                             \
    {                                                                                              \
        if (!Graphics::Vulkan::hasExtension(                                                       \
                vk::raii::Context{}.enumerateInstanceExtensionProperties(),                        \
                "VK_EXT_headless_surface"))                                                        \
        {                                                                                          \
            GTEST_SKIP() << "VK_EXT_headless_surface not available";                               \
        }                                                                                          \
    } while (false)

/** What @p call throws as std::invalid_argument; fails the test if it does not throw one. */
template <class Call>
std::string
invalidArgumentOf(Call&& call)
{
    try
    {
        std::ignore = call();
    }
    catch (const std::invalid_argument& error)
    {
        return error.what();
    }
    ADD_FAILURE() << "No std::invalid_argument was thrown.";

    return {};
}

constexpr std::string_view gNoDeclarations;
} // namespace

// ---------------------------------------------------------------------------------------------
// What a shader must be, checked when the shaders are loaded (E2-E7)
// ---------------------------------------------------------------------------------------------

/**
 * E2: a member of the Material block is a scalar, a vector, a mat3 or a mat4 of 32-bit numbers, or
 * a TextureRef. An array, a row-major matrix, another struct, and the other types are refused, and
 * the error names the shader and the member.
 */
TEST_F(MaterialTest, E2_aMemberOfAnotherKindIsRefused)
{
    const auto error = [this](std::string_view declarations, std::string_view read)
    {
        return loadErrorOf(gVertexSource, fragmentSource(declarations, read));
    };

    const std::string array =
        error("VOG_MATERIAL { float weights[3]; } gMaterials[];", "vec4(gMaterial.weights[1])");
    EXPECT_TRUE(contains(array,
                         "The Material block of shader \"Case\" has the member \"weights\", "
                         "which is an array; a material parameter cannot be an array."))
        << array;

    const std::string rowMajor =
        error("VOG_MATERIAL { layout(row_major) mat4 m; } gMaterials[];", "gMaterial.m[0]");
    EXPECT_TRUE(contains(rowMajor,
                         "has the member \"m\", a row_major matrix; declare material "
                         "matrices column-major (the default)."))
        << rowMajor;

    const std::string structure =
        error("struct Foo { float a; };\nVOG_MATERIAL { Foo foo; } gMaterials[];",
              "vec4(gMaterial.foo.a)");
    EXPECT_TRUE(contains(structure,
                         "has the member \"foo\" of type struct Foo; the only struct a "
                         "Material block takes is TextureRef."))
        << structure;

    const std::string matrix =
        error("VOG_MATERIAL { mat2 m; } gMaterials[];", "vec4(gMaterial.m[0], 0.0, 0.0)");
    EXPECT_TRUE(contains(matrix,
                         "has the member \"m\" of type mat2, which a material cannot set; the "
                         "types are float, vec2-4, int, ivec2-4, uint, uvec2-4, mat3, mat4 "
                         "and TextureRef."))
        << matrix;

    const std::string wide =
        error("VOG_MATERIAL { double d; } gMaterials[];", "vec4(float(gMaterial.d))");
    EXPECT_TRUE(contains(wide, "has the member \"d\" of type double, which a material cannot set"))
        << wide;

    // The accepted types load.
    EXPECT_EQ(
        error("VOG_MATERIAL { mat3 a; mat4 b; uvec3 c; ivec2 d; TextureRef e; } gMaterials[];",
              "vec4(gMaterial.c.x) + gMaterial.b[0]"),
        "");
}

/** E3: the struct TextureRef of the Material block has the two members of the include. */
TEST_F(MaterialTest, E3_aTextureRefOfAnotherShapeIsRefused)
{
    const auto error = [this](std::string_view textureRef)
    {
        return loadErrorOf(
            gPlainVertexSource,
            standaloneFragmentSource(
                std::format("{}\nlayout(descriptor_heap, descriptor_stride = 128) uniform Material "
                            "{{ vec4 tint; TextureRef albedo; }} gMaterials[];",
                            textureRef),
                "gMaterials[0].tint * float(gMaterials[0].albedo.index)"));
    };
    constexpr std::string_view kMessage =
        "The struct TextureRef of shader \"Case\" is not { uint index; uint samplerIndex; }.";

    EXPECT_TRUE(
        contains(error("struct TextureRef { uint index; float samplerIndex; };"), kMessage));
    EXPECT_TRUE(contains(error("struct TextureRef { uint index; uint wrap; };"), kMessage));
    EXPECT_TRUE(contains(error("struct TextureRef { uint index; };"), kMessage));
    EXPECT_TRUE(contains(error("struct TextureRef { uint samplerIndex; uint index; };"), kMessage));
    EXPECT_EQ(error("struct TextureRef { uint index; uint samplerIndex; };"), "");
}

/** E4: a Material block is at most 16384 bytes, the spec's minimum of maxUniformBufferRange. */
TEST_F(MaterialTest, E4_aBlockOverTheUniformRangeIsRefused)
{
    const auto error = [this](std::string_view elements)
    {
        return loadErrorOf(
            gVertexSource,
            fragmentSource(std::format("VOG_MATERIAL {{ vec4 big[{}]; }} gMaterials[];", elements),
                           "gMaterial.big[0]"));
    };

    const std::string over = error("1025");
    EXPECT_TRUE(
        contains(over,
                 "The Material block of shader \"Case\" is 16400 bytes, more than the 16384 "
                 "a uniform block holds."))
        << over;

    // 16384 bytes pass the size rule, and fail the one about arrays instead.
    const std::string exact = error("1024");
    EXPECT_FALSE(contains(exact, "more than the")) << exact;
    EXPECT_TRUE(contains(exact, "which is an array")) << exact;
}

/**
 * E5: the members of the push block of a program are members of DrawPushData, at its offsets. A
 * reordered block (mat4; uint frame; uint material) fails when the shaders are loaded, which is
 * when the Renderer is made, and so does a member of another type or one that DrawPushData lacks.
 * A program may declare fewer members, or no push block.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(MaterialTest, E5_aPushBlockThatIsNotDrawPushDataIsRefused)
{
    const auto error = [this](std::string_view members)
    {
        return loadErrorOf(std::format(R"(#version 450
layout(push_constant) uniform Draw {{ {} }} gDraw;
void main() {{ gl_Position = gDraw.model * vec4(0.0, 0.0, 0.0, 1.0); }}
)",
                                       members),
                           standaloneFragmentSource(gNoDeclarations, "vec4(1.0)"));
    };
    constexpr std::string_view kMessage =
        "Shader \"Case\" does not take the renderer's push data (mat4 model at 0, uint material at "
        "64, uint frame at 68).";

    const std::string reordered = error("mat4 model; uint frame; uint material;");
    EXPECT_TRUE(contains(reordered, kMessage)) << reordered;
    EXPECT_TRUE(contains(reordered,
                         "Member \"frame\" of the push block is at offset 64 in the shader "
                         "and 68 in C++."))
        << reordered;

    const std::string extra = error("mat4 model; uint material; uint frame; uint extra;");
    EXPECT_TRUE(contains(extra, kMessage)) << extra;
    EXPECT_TRUE(contains(extra, "\"extra\"")) << extra;

    const std::string wrongType = error("mat4 model; float material; uint frame;");
    EXPECT_TRUE(contains(wrongType, kMessage)) << wrongType;
    EXPECT_TRUE(contains(wrongType, "has the type float in the shader and uint in C++"))
        << wrongType;

    EXPECT_EQ(error("mat4 model; uint material; uint frame;"), "");
    EXPECT_EQ(error("mat4 model;"), "");
    EXPECT_EQ(
        loadErrorOf(gPlainVertexSource, standaloneFragmentSource(gNoDeclarations, "vec4(1.0)")),
        "");
}

/** E6: the renderer fills the Frame block and the Material block; another heap block is refused. */
TEST_F(MaterialTest, E6_anotherHeapBlockIsRefused)
{
    const std::string error =
        loadErrorOf(gVertexSource,
                    fragmentSource("VOG_MATERIAL { vec4 tint; } gMaterials[];\n"
                                   "layout(VOG_HEAP) uniform Light { vec4 color; } gLights[];",
                                   "gMaterial.tint * gLights[0].color"));

    EXPECT_TRUE(contains(error,
                         "Shader \"Case\" reads the heap block \"Light\"; the renderer fills only "
                         "the blocks named \"Frame\" and \"Material\"."))
        << error;
}

/** E7: the renderer backs the Material block with a uniform buffer, so a `buffer` block is refused.
 */
TEST_F(MaterialTest, E7_aStorageMaterialBlockIsRefused)
{
    const std::string error =
        loadErrorOf(gVertexSource,
                    fragmentSource("layout(VOG_HEAP) buffer Material { vec4 tint; } gMaterials[];",
                                   "gMaterials[0].tint"));

    EXPECT_TRUE(contains(error,
                         "The Material block of shader \"Case\" is a storage (buffer) block; the "
                         "renderer backs it with a uniform buffer, so declare it uniform "
                         "(VOG_MATERIAL does)."))
        << error;
}

/** The Frame block of a shader is the block that the renderer writes, in a uniform buffer. */
TEST_F(MaterialTest, aFrameBlockThatIsNotTheRenderersIsRefused)
{
    const auto error = [this](std::string_view declaration)
    {
        return loadErrorOf(gPlainVertexSource,
                           standaloneFragmentSource(declaration, "gFrames[0].view[0]"));
    };

    const std::string shorter = error(
        "layout(descriptor_heap, descriptor_stride = 128) uniform Frame { mat4 view; } gFrames[];");
    EXPECT_TRUE(contains(shorter,
                         "The Frame block of shader \"Case\" is not the block that the "
                         "renderer writes (FrameBlock)."))
        << shorter;
    EXPECT_TRUE(contains(shorter, "\"projection\"")) << shorter;

    const std::string storage = error(
        "layout(descriptor_heap, descriptor_stride = 128) buffer Frame { mat4 view; } gFrames[];");
    EXPECT_TRUE(contains(storage, "The Frame block of shader \"Case\" is a storage (buffer) block"))
        << storage;
}

/** The stride of a heap block is the size of a slot of the descriptor heap, or the load fails. */
TEST_F(MaterialTest, aMaterialBlockWithAnotherStrideIsRefused)
{
    const std::string error = loadErrorOf(
        gPlainVertexSource,
        standaloneFragmentSource(
            "layout(descriptor_heap, descriptor_stride = 64) uniform Material { vec4 tint; } "
            "gMaterials[];",
            "gMaterials[0].tint"));

    EXPECT_TRUE(contains(error, "Failed to load shader \"Case\"")) << error;
    EXPECT_TRUE(contains(error, "the heap block \"Material\" with a stride of 64 bytes")) << error;
}

/** A shader is a pair of files: a stage without its partner fails when the library is made. */
TEST_F(MaterialTest, aShaderNeedsBothStages)
{
    ShaderDirectory directory;
    std::ofstream{directory.path() / "Alone.vert.glsl"} << gPlainVertexSource;
    const std::string error = loadError(directory);
    EXPECT_TRUE(contains(error, "The shader \"Alone\" has a vertex stage but no Alone.frag.glsl"))
        << error;

    std::filesystem::remove(directory.path() / "Alone.vert.glsl");
    std::ofstream{directory.path() / "Alone.frag.glsl"} << "#version 450\nvoid main() {}\n";
    const std::string other = loadError(directory);
    EXPECT_TRUE(contains(other, "The shader \"Alone\" has a fragment stage but no Alone.vert.glsl"))
        << other;
}

/** A shader that does not compile names its file and keeps the compiler's log. */
TEST_F(MaterialTest, aShaderThatDoesNotCompileNamesItsFile)
{
    const std::string error =
        loadErrorOf(gPlainVertexSource, "#version 450\nvoid main() { oops }\n");

    EXPECT_TRUE(contains(error, "Failed to load shader \"Case\"")) << error;
    EXPECT_TRUE(contains(error, "Failed to compile")) << error;
    EXPECT_TRUE(contains(error, "Case.frag.glsl")) << error;
}

/**
 * A heap block that no stage reads is not in the SPIR-V, so a shader that declares a Material block
 * and never reads it has none: no parameters, and a material of it takes none.
 */
TEST_F(MaterialTest, aMaterialBlockNoStageReadsIsNoBlock)
{
    VOG_REQUIRE_HEADLESS();

    ShaderDirectory directory;
    directory.add("Unread",
                  gVertexSource,
                  fragmentSource("VOG_MATERIAL { vec4 tint; } gMaterials[];", "vec4(1.0)"));
    const auto renderer = makeRenderer(directory.path());

    const Shader shader = renderer->getShader("Unread");
    EXPECT_TRUE(shader.parameters().empty());
    EXPECT_TRUE(renderer->createMaterial({.shader = shader}));

    const std::string error = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial(
                {.shader = shader, .parameters = {{"tint", Math::Vector4f{1.0f}}}});
        });
    EXPECT_EQ(error,
              "Shader \"Unread\" reads no Material block (a block no stage reads is compiled out), "
              "so it takes no parameters; got \"tint\".");
}

// ---------------------------------------------------------------------------------------------
// The values of a material (V1-V5), and how they are packed
// ---------------------------------------------------------------------------------------------

/** E1: the shader directory has no shader of that name. */
TEST_F(MaterialTest, E1_anUnknownShaderIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto renderer = makeRenderer(sharedTestShaderPath());

    EXPECT_TRUE(renderer->getShader("Textured"));
    EXPECT_EQ(renderer->getShader("Textured"), renderer->getShader("Textured"));
    EXPECT_FALSE(Shader{});

    const std::string error = invalidArgumentOf(
        [&]
        {
            return renderer->getShader("NoSuchShader");
        });
    EXPECT_TRUE(contains(error, "No shader \"NoSuchShader\" in ")) << error;
}

/** V1: a parameter that is no member of the Material block, and a shader that has no block. */
TEST_F(MaterialTest, V1_aParameterOfNoMemberIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto   renderer = makeRenderer(sharedTestShaderPath());
    const Shader textured = renderer->getShader("Textured");

    ASSERT_EQ(textured.parameters().size(), 2u);
    EXPECT_EQ(textured.parameters()[0].name, "tint");
    EXPECT_EQ(textured.parameters()[1].name, "albedo");

    const std::string error = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial(
                {.shader = textured, .parameters = {{"tnit", Math::Vector4f{1.0f}}}});
        });
    EXPECT_EQ(error,
              "Shader \"Textured\" has no material parameter \"tnit\"; its parameters are tint "
              "(vec4), albedo (TextureRef).");

    const std::string noBlock = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial({.shader     = renderer->getShader("WorldSpace"),
                                             .parameters = {{"tint", Math::Vector4f{1.0f}}}});
        });
    EXPECT_EQ(noBlock,
              "Shader \"WorldSpace\" reads no Material block (a block no stage reads is compiled "
              "out), so it takes no parameters; got \"tint\".");
}

/** V2: a parameter given twice. */
TEST_F(MaterialTest, V2_aParameterGivenTwiceIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto renderer = makeRenderer(sharedTestShaderPath());

    const std::string error = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial({.shader     = renderer->getShader("Textured"),
                                             .parameters = {{"tint", Math::Vector4f{1.0f}},
                                                            {"albedo", makeTexture(*renderer)},
                                                            {"tint", Math::Vector4f{0.0f}}}});
        });

    EXPECT_EQ(error, "Material parameter \"tint\" is set twice.");
}

/** V3: a value of another type than its member; an int or a float for the other adds a hint. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(MaterialTest, V3_aValueOfAnotherTypeIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto renderer = makeRenderer(sharedTestShaderPath());
    const auto make     = [&](MaterialParameter parameter)
    {
        return invalidArgumentOf(
            [&]
            {
                return renderer->createMaterial({.shader     = renderer->getShader("Textured"),
                                                 .parameters = {std::move(parameter)}});
            });
    };

    EXPECT_EQ(make({"tint", 1.0f}),
              "Material parameter \"tint\" of shader \"Textured\" is a vec4; got a float.");
    EXPECT_EQ(make({"albedo", Math::Vector4f{1.0f}}),
              "Material parameter \"albedo\" of shader \"Textured\" is a TextureRef; got a vec4.");

    // A float where the member is an int, the hint of the literals.
    ShaderDirectory directory;
    directory.add("Scalars",
                  gVertexSource,
                  fragmentSource("VOG_MATERIAL { float f; uint u; ivec2 v; } gMaterials[];",
                                 "vec4(gMaterial.f + float(gMaterial.u) + float(gMaterial.v.x))"));
    const auto scalarRenderer = makeRenderer(directory.path());
    const auto scalar         = [&](MaterialParameter parameter)
    {
        return invalidArgumentOf(
            [&]
            {
                return scalarRenderer->createMaterial(
                    {.shader     = scalarRenderer->getShader("Scalars"),
                     .parameters = {std::move(parameter)}});
            });
    };
    EXPECT_EQ(
        scalar({"f", 1}),
        "Material parameter \"f\" of shader \"Scalars\" is a float; got an int. Write 1.0f for "
        "a float, 1u for a uint.");
    EXPECT_EQ(
        scalar({"u", 1}),
        "Material parameter \"u\" of shader \"Scalars\" is a uint; got an int. Write 1.0f for "
        "a float, 1u for a uint.");
    EXPECT_EQ(scalar({"v", Math::Vector2u{1u}}),
              "Material parameter \"v\" of shader \"Scalars\" is an ivec2; got a uvec2. Write 1.0f "
              "for a float, 1u for a uint.");
    // No hint where the shapes differ.
    EXPECT_EQ(scalar({"v", 1.0f}),
              "Material parameter \"v\" of shader \"Scalars\" is an ivec2; got a float.");
}

/** V4: every member of the block needs a value; the error lists the ones that have none. */
TEST_F(MaterialTest, V4_aMemberWithoutAValueIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto   renderer = makeRenderer(sharedTestShaderPath());
    const Shader textured = renderer->getShader("Textured");

    const std::string both = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial({.shader = textured});
        });
    EXPECT_EQ(both, "Shader \"Textured\" needs values for tint (vec4), albedo (TextureRef).");

    const std::string one = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial(
                {.shader = textured, .parameters = {{"tint", Math::Vector4f{1.0f}}}});
        });
    EXPECT_EQ(one, "Shader \"Textured\" needs values for albedo (TextureRef).");
}

/** V5: a texture parameter that is an empty Texture. */
TEST_F(MaterialTest, V5_anEmptyTextureIsRefused)
{
    VOG_REQUIRE_HEADLESS();
    const auto renderer = makeRenderer(sharedTestShaderPath());

    const std::string error = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial(
                {.shader     = renderer->getShader("Textured"),
                 .parameters = {{"tint", Math::Vector4f{1.0f}}, {"albedo", Texture{}}}});
        });

    EXPECT_EQ(error, "Material parameter \"albedo\" of shader \"Textured\" is a null texture.");
}

/** A name in the description starts the message; a material needs a shader. */
TEST_F(MaterialTest, theNameOfAMaterialStartsItsErrors)
{
    VOG_REQUIRE_HEADLESS();
    const auto renderer = makeRenderer(sharedTestShaderPath());

    const std::string named = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial(
                {.name = "Hero", .shader = renderer->getShader("Textured")});
        });
    EXPECT_TRUE(named.starts_with("Material \"Hero\": Shader \"Textured\" needs values")) << named;

    const std::string noShader = invalidArgumentOf(
        [&]
        {
            return renderer->createMaterial({.name = "Hero"});
        });
    EXPECT_TRUE(noShader.starts_with("Material \"Hero\": A material needs a shader")) << noShader;
}

namespace
{
/** A shader that has every type of parameter, and after the texture a vec4, a TextureRef and a
 *  float, so that what follows a member that takes more room than it fills is tested. */
constexpr std::string_view gAllTypesDeclarations = R"(VOG_MATERIAL {
    float f1; vec2 f2; vec3 f3; vec4 f4;
    int i1; ivec2 i2; ivec3 i3; ivec4 i4;
    uint u1; uvec2 u2; uvec3 u3; uvec4 u4;
    mat3 m3; mat4 m4;
    TextureRef tex;
    vec4 tint; TextureRef albedo; float rough;
} gMaterials[];)";

constexpr std::string_view gAllTypesRead =
    "gMaterial.f4 + gMaterial.tint + vec4(gMaterial.rough, float(gMaterial.albedo.index), "
    "float(gMaterial.tex.samplerIndex), float(gMaterial.u4.w))";

/** A value for every member of the all-types shader, in the order of the block. */
// The numbers only tell the values apart.
// NOLINTBEGIN(readability-magic-numbers)
std::vector<MaterialParameter>
allTypesParameters(const Texture& texture, const Texture& nearest)
{
    return {{"f1", 1.5f},
            {"f2", Math::Vector2f{2.5f, 3.5f}},
            {"f3", Math::Vector3f{4.5f, 5.5f, 6.5f}},
            {"f4", Math::Vector4f{7.5f, 8.5f, 9.5f, 10.5f}},
            {"i1", -1},
            {"i2", Math::Vector2i{-2, -3}},
            {"i3", Math::Vector3i{-4, -5, -6}},
            {"i4", Math::Vector4i{-7, -8, -9, -10}},
            {"u1", 11u},
            {"u2", Math::Vector2u{12u, 13u}},
            {"u3", Math::Vector3u{14u, 15u, 16u}},
            {"u4", Math::Vector4u{17u, 18u, 19u, 20u}},
            {"m3", Math::Matrix3x3f{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}},
            {"m4",
             Math::Matrix4x4f{11.0f,
                              12.0f,
                              13.0f,
                              14.0f,
                              15.0f,
                              16.0f,
                              17.0f,
                              18.0f,
                              19.0f,
                              20.0f,
                              21.0f,
                              22.0f,
                              23.0f,
                              24.0f,
                              25.0f,
                              26.0f}},
            {"tex", texture},
            {"tint", Math::Vector4f{0.25f, 0.5f, 0.75f, 1.0f}},
            {"albedo", nearest},
            {"rough", 0.125f}};
}

// NOLINTEND(readability-magic-numbers)

/** The bytes that the value of @p parameter takes in a block whose matrices are @p stride apart. */
std::vector<std::byte>
expectedBytes(const MaterialValue& value, std::uint32_t stride)
{
    std::vector<std::byte> bytes;
    const auto             append = [&bytes](const auto& part)
    {
        const auto view = std::as_bytes(std::span{&part, 1u});
        bytes.insert(bytes.end(), view.begin(), view.end());
    };

    std::visit(
        [&](const auto& held)
        {
            using Held = std::decay_t<decltype(held)>;
            if constexpr (std::same_as<Held, Texture>)
            {
                const auto& data = HandleTestAccess::data(held);
                append(std::uint32_t{data.texture->heapIndex()});
                append(static_cast<std::uint32_t>(data.filter));
            }
            else if constexpr (std::same_as<Held, Math::Matrix3x3f> ||
                               std::same_as<Held, Math::Matrix4x4f>)
            {
                for (std::uint32_t column = 0u; column < Held::length(); ++column)
                {
                    const std::size_t first = bytes.size();
                    append(held[column]);
                    // The columns are stride apart: the gap stays zero.
                    bytes.resize(first + stride);
                }
                bytes.resize(bytes.size() - stride + sizeof(held[0]));
            }
            else
            {
                append(held);
            }
        },
        value);

    return bytes;
}
} // namespace

/**
 * Every type of parameter is packed at the offset the shader reports, not at one the test or the
 * packer assumes: the offsets here come from the reflection of the program. A mat3 has a gap
 * between its columns, and the members after it and after a texture are where the reflection puts
 * them. Everything that no member covers is zero. The parameters are given in the opposite order
 * of the block: a value goes to its member by name.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(MaterialTest, everyTypeIsPackedAtItsReflectedOffset)
{
    VOG_REQUIRE_HEADLESS();

    ShaderDirectory directory;
    directory.add("AllTypes", gVertexSource, fragmentSource(gAllTypesDeclarations, gAllTypesRead));
    const auto    renderer = makeRenderer(directory.path());
    const Texture linear   = makeTexture(*renderer, Filter::eLinear);
    const Texture nearest  = makeTexture(*renderer, Filter::eNearest);

    std::vector<MaterialParameter> parameters = allTypesParameters(linear, nearest);
    ASSERT_EQ(parameters.size(), 18u);
    std::ranges::reverse(parameters);
    const Shader   shader = renderer->getShader("AllTypes");
    const Material material =
        renderer->createMaterial({.shader = shader, .parameters = parameters});

    const auto& program   = HandleTestAccess::data(shader).program;
    const auto  reflected = std::ranges::find(
        program->heapBlocks, "Material", &Graphics::Vulkan::Shader::Reflection::HeapBlock::name);
    ASSERT_NE(reflected, program->heapBlocks.end());

    auto              mapping = HandleTestAccess::data(material).block->mapForWrite();
    const auto        block   = mapping.bytes().first(reflected->size);
    std::vector<bool> covered(block.size(), false);
    for (const MaterialParameter& parameter : parameters)
    {
        const auto member =
            std::ranges::find(reflected->members,
                              parameter.name,
                              &Graphics::Vulkan::Shader::Reflection::BlockMember::name);
        ASSERT_NE(member, reflected->members.end()) << parameter.name;

        const std::vector<std::byte> expected =
            expectedBytes(parameter.value, member->matrixStride);
        ASSERT_LE(member->offset + expected.size(), block.size()) << parameter.name;
        EXPECT_TRUE(std::ranges::equal(block.subspan(member->offset, expected.size()), expected))
            << parameter.name << " at offset " << member->offset;
        std::fill_n(covered.begin() + member->offset, expected.size(), true);
    }

    // The gaps are zero.
    for (std::size_t i = 0u; i < block.size(); ++i)
    {
        if (!covered[i])
        {
            EXPECT_EQ(block[i], std::byte{0}) << "byte " << i;
        }
    }

    // The test is about offsets that differ from a tight packing.
    const auto offsetOf = [&](std::string_view name)
    {
        return std::ranges::find(reflected->members,
                                 name,
                                 &Graphics::Vulkan::Shader::Reflection::BlockMember::name)
            ->offset;
    };
    EXPECT_EQ(offsetOf("m3") % 16u, 0u);
    EXPECT_GT(offsetOf("m4"), offsetOf("m3") + (3u * sizeof(Math::Vector3f)));
    EXPECT_EQ(HandleTestAccess::data(material).textures.size(), 2u);
}

/** Material::parameters() returns the values it was made with, in the order given; the render
 *  state is stored with the material. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(MaterialTest, parametersAreReadBackAsGiven)
{
    VOG_REQUIRE_HEADLESS();

    ShaderDirectory directory;
    directory.add("AllTypes", gVertexSource, fragmentSource(gAllTypesDeclarations, gAllTypesRead));
    const auto    renderer = makeRenderer(directory.path());
    const Texture linear   = makeTexture(*renderer, Filter::eLinear);
    const Texture nearest  = makeTexture(*renderer, Filter::eNearest);

    std::vector<MaterialParameter> parameters = allTypesParameters(linear, nearest);
    std::ranges::reverse(parameters);
    constexpr RenderState kState{
        .blend = BlendMode::eAdditive, .depth = DepthMode::eTestOnly, .cull = CullMode::eBack};
    const Material material = renderer->createMaterial(
        {.shader = renderer->getShader("AllTypes"), .parameters = parameters, .state = kState});

    // The state is kept as given; applying it is for the renderer's pipelines later.
    EXPECT_EQ(HandleTestAccess::data(material).state, kState);

    const std::span<const MaterialParameter> read = material.parameters();
    ASSERT_EQ(read.size(), parameters.size());
    for (std::size_t i = 0u; i < read.size(); ++i)
    {
        EXPECT_EQ(read[i].name, parameters[i].name);
        EXPECT_TRUE(read[i].value == parameters[i].value) << parameters[i].name;
    }

    EXPECT_TRUE(Material{}.parameters().empty());
}
} // namespace VOG::Tests
