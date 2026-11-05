#include <VOG/Graphics/Vulkan/Attachment/RenderTarget.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferPool.hpp>
#include <VOG/Graphics/Vulkan/CommandBufferRecorder.hpp>
#include <VOG/Graphics/Vulkan/GLSLCompiler.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Shader.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#include "HeapShaderSources.hpp"
#include "VulkanFixture.hpp"
#include "VulkanValidationFixture.hpp"

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{

const std::string gMinimalVertexShader = R"(
#version 450
layout(location = 0) in vec4 inPosition;
void main() { gl_Position = inPosition; }
)";

const std::string gMinimalFragmentShader = R"(
#version 450
layout(location = 0) out vec4 outColor;
void main() { outColor = vec4(1.0); }
)";

const std::string gVertexShaderPushConstants = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { vec4 transform; } pc;
void main() { gl_Position = inPosition + pc.transform; }
)";

// Push data that overlaps in bytes with the other stage: a mat4 covers bytes 0 to 63, a uint at 16
// lies inside.
const std::string gVertexShaderPushMatrix = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { mat4 m; } pc;
void main() { gl_Position = pc.m * inPosition; }
)";

const std::string gFragmentShaderPushInsideMatrix = R"(
#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { layout(offset = 16) uint a; } pc;
void main() { outColor = vec4(float(pc.a)); }
)";

const std::string gVertexShaderPushInsideMatrix = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { layout(offset = 16) uint a; } pc;
void main() { gl_Position = inPosition * float(pc.a); }
)";

const std::string gFragmentShaderPushMatrix = R"(
#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { mat4 m; } pc;
void main() { outColor = pc.m[0]; }
)";

// A push block that a stage declares and never reads.
const std::string gVertexShaderPushUnread = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { mat4 m; uint a; } pc;
void main() { gl_Position = inPosition; }
)";

const std::string gFragmentShaderPushUnread = R"(
#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { mat4 m; uint a; } pc;
void main() { outColor = vec4(1.0); }
)";

// The same block, read.
const std::string gVertexShaderPushRead = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { mat4 m; uint a; } pc;
void main() { gl_Position = pc.m * inPosition * float(pc.a); }
)";

// Part of the block of gVertexShaderPushRead, at the offsets it has there.
const std::string gFragmentShaderPushTail = R"(
#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { layout(offset = 64) uint a; } pc;
void main() { outColor = vec4(float(pc.a)); }
)";

// The member "a" of gVertexShaderPushUnread, 64 bytes later: it overlaps nothing there.
const std::string gFragmentShaderPushAtOtherOffset = R"(
#version 450
layout(location = 0) out vec4 outColor;
layout(push_constant) uniform PC { layout(offset = 128) uint a; } pc;
void main() { outColor = vec4(float(pc.a)); }
)";

const std::string gVertexShaderPushAtOtherOffset = R"(
#version 450
layout(location = 0) in vec4 inPosition;
layout(push_constant) uniform PC { layout(offset = 128) uint a; } pc;
void main() { gl_Position = inPosition * float(pc.a); }
)";

ShaderProgramPtr
makeProgram(VulkanFixture& fixture, const std::string& vertex, const std::string& fragment)
{
    return ShaderProgram::create(
        {.vertex   = {.shader = fixture.createShader(Shader::ShadingStage::eVertex, vertex)},
         .fragment = {.shader = fixture.createShader(Shader::ShadingStage::eFragment, fragment)}});
}

// Stand-ins for the vector and matrix types of a math library: BlockLayout recognizes them by their
// members, as it does glm's.
template <int Components>
struct Vector
{
    using value_type = float;

    static constexpr int
    length()
    {
        return Components;
    }

    std::array<float, Components> components;
};

template <class Column, int Columns>
struct Matrix
{
    using col_type = Column;

    static constexpr int
    length()
    {
        return Columns;
    }

    std::array<Column, Columns> columns;
};

using Vec3 = Vector<3>;
using Vec4 = Vector<4>;
using Mat4 = Matrix<Vec4, 4>;

/** A struct that has the layout of `vec4 tint; mat4 basis; float rough;` in a std140 block. */
struct MaterialBlock
{
    Vec4  tint;
    Mat4  basis;
    float rough;
};

BlockLayout
materialLayout()
{
    return BlockLayout::of<MaterialBlock>({{"tint", &MaterialBlock::tint},
                                           {"basis", &MaterialBlock::basis},
                                           {"rough", &MaterialBlock::rough}});
}

const std::string gMaterialMembers = "vec4 tint; mat4 basis; float rough;";

const std::string gMaterialRead =
    "gMaterials[pushData.index].tint * gMaterials[pushData.index].basis[0] + "
    "vec4(gMaterials[pushData.index].rough)";

/** The program of a vertex stage with no resources and the fragment stage that reads @p block. */
ShaderProgramPtr
makeMaterialProgram(VulkanFixture& fixture, const std::string& block, const std::string& read)
{
    return makeProgram(
        fixture, gMinimalVertexShader, heapShader(Shader::ShadingStage::eFragment, block, read));
}

/** What the call throws as std::invalid_argument; fails the test if it does not throw. */
std::string
invalidArgumentOf(const std::function<void()>& call)
{
    try
    {
        call();
    }
    catch (const std::invalid_argument& error)
    {
        return error.what();
    }
    ADD_FAILURE() << "No std::invalid_argument was thrown.";

    return {};
}

GraphicsPipeline::Parameters
makeMinimalPipelineParameters(ShaderProgramPtr program)
{
    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    return GraphicsPipeline::Parameters{
        .shading       = program,
        .vertexLayout  = {.bindingDescription   = {{.binding   = 0u,
                                                    .stride    = 16u,
                                                    .inputRate = vk::VertexInputRate::eVertex}},
                          .attributeDescription = {{.location = 0u,
                                                    .binding  = 0u,
                                                    .format   = vk::Format::eR32G32B32A32Sfloat,
                                                    .offset   = 0u}}},
        .rasterizer    = {.cullMode = CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {},
        .blending      = {.attachments = {{.colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {.colorAttachmentFormats = {vk::Format::eR32G32B32A32Sfloat}},
    };
}

} // namespace

// ============================================================
// Structural tests (VulkanFixture)
// ============================================================

TEST_F(VulkanFixture, ShaderProgram_create)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    EXPECT_NO_THROW(std::ignore = ShaderProgram::create(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));
}

TEST_F(VulkanFixture, ShaderProgram_pushConstants_reflectedCorrectly)
{
    auto vert = createShader(Shader::ShadingStage::eVertex, gVertexShaderPushConstants);
    auto frag = createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader);

    std::shared_ptr<ShaderProgram> program;
    ASSERT_NO_THROW(program = ShaderProgram::create(
                        {.vertex = {.shader = vert}, .fragment = {.shader = frag}}));

    ASSERT_EQ(program->pushConstants.size(), 1u);
    EXPECT_EQ(program->pushConstants[0].offset, 0u);
    EXPECT_EQ(program->pushConstants[0].size, 16u);
    EXPECT_EQ(program->pushConstants[0].name, "transform");
}

/**
 * A push block that only one stage declares is the block of the program: the stage without one
 * contributes nothing. glslang keeps a push block that a stage declares and never reads (U5 of the
 * design, measured here on the pinned glslang), so a stage that reads nothing from the block still
 * reflects all of it; the merge is over the blocks the stages declare, not over what they read.
 */
TEST_F(VulkanFixture, ShaderProgram_pushBlockOfOneStageIsTheBlockOfTheProgram)
{
    const auto expectBlock = [](const ShaderProgramPtr& program)
    {
        ASSERT_EQ(program->pushConstants.size(), 2u);
        EXPECT_EQ(program->pushConstants[0].name, "m");
        EXPECT_EQ(program->pushConstants[0].offset, 0u);
        EXPECT_EQ(program->pushConstants[0].size, 64u);
        EXPECT_EQ(program->pushConstants[1].name, "a");
        EXPECT_EQ(program->pushConstants[1].offset, 64u);
    };

    SCOPED_TRACE("declared and read in the vertex stage; the fragment stage has none");
    expectBlock(makeProgram(*this, gVertexShaderPushRead, gMinimalFragmentShader));

    SCOPED_TRACE("declared in the vertex stage and never read");
    expectBlock(makeProgram(*this, gVertexShaderPushUnread, gMinimalFragmentShader));

    SCOPED_TRACE("declared in the fragment stage and never read");
    expectBlock(makeProgram(*this, gMinimalVertexShader, gFragmentShaderPushUnread));

    SCOPED_TRACE("never read in either stage");
    expectBlock(makeProgram(*this, gVertexShaderPushUnread, gFragmentShaderPushUnread));

    // The same with the descriptor-heap extension on: the shaders of the renderer declare it.
    SCOPED_TRACE("never read, with the heap extension");
    const auto program = makeProgram(*this,
                                     heapShader(Shader::ShadingStage::eVertex, "", "vec4(1.0)"),
                                     heapShader(Shader::ShadingStage::eFragment, "", "vec4(1.0)"));
    ASSERT_EQ(program->pushConstants.size(), 1u);
    EXPECT_EQ(program->pushConstants[0].name, "index");
}

/**
 * The stages declare the parts of the push block they use: members that overlap must be identical,
 * the others are added. The merged block is sorted by offset, whichever stage declares what.
 */
TEST_F(VulkanFixture, ShaderProgram_pushBlocksOfBothStagesAreUnited)
{
    for (const auto& [vertex, fragment] :
         {std::pair{gVertexShaderPushRead, gFragmentShaderPushTail},
          std::pair{gVertexShaderPushUnread, gFragmentShaderPushTail}})
    {
        const auto program = makeProgram(*this, vertex, fragment);
        ASSERT_EQ(program->pushConstants.size(), 2u);
        EXPECT_EQ(program->pushConstants[0].name, "m");
        EXPECT_EQ(program->pushConstants[1].name, "a");
    }

    // Only the vertex stage has the matrix; the fragment stage adds the uint behind it.
    const auto program = makeProgram(*this, gVertexShaderPushMatrix, gFragmentShaderPushTail);
    ASSERT_EQ(program->pushConstants.size(), 2u);
    EXPECT_EQ(program->pushConstants[0].name, "m");
    EXPECT_EQ(program->pushConstants[1].name, "a");
    EXPECT_EQ(program->pushConstants[1].offset, 64u);
}

/**
 * Two stages that read the same heap block are one block of the program, with the members of both
 * and the size of the larger. A block that only one stage reads is the program's too.
 */
TEST_F(VulkanFixture, ShaderProgram_heapBlocksOfBothStagesAreMergedByName)
{
    constexpr auto kStride = "descriptor_stride = 128";

    const auto program =
        makeProgram(*this,
                    heapShader(Shader::ShadingStage::eVertex,
                               materialBlock("uniform", kStride, "vec4 tint;"),
                               "gMaterials[pushData.index].tint"),
                    heapShader(Shader::ShadingStage::eFragment,
                               materialBlock("uniform", kStride, "vec4 tint; vec4 extra;"),
                               "gMaterials[pushData.index].extra"));

    ASSERT_EQ(program->heapBlocks.size(), 1u);
    const auto& block = program->heapBlocks[0];
    EXPECT_EQ(block.name, "Material");
    EXPECT_EQ(block.size, 32u);
    ASSERT_EQ(block.members.size(), 2u);
    EXPECT_EQ(block.members[0].name, "tint");
    EXPECT_EQ(block.members[1].name, "extra");
    EXPECT_EQ(block.members[1].offset, 16u);

    const auto vertexOnly = makeProgram(*this,
                                        heapShader(Shader::ShadingStage::eVertex,
                                                   materialBlock("uniform", kStride, "vec4 tint;"),
                                                   "gMaterials[pushData.index].tint"),
                                        gMinimalFragmentShader);
    ASSERT_EQ(vertexOnly->heapBlocks.size(), 1u);
    EXPECT_EQ(vertexOnly->heapBlocks[0].name, "Material");
}

/**
 * Stages that declare a heap block differently cannot be one program: an overlapping member with
 * another type, a member of one name at two offsets (it overlaps nothing, but it is not one
 * member), or a uniform block in one stage and a storage block in the other. The error names the
 * block.
 */
TEST_F(VulkanFixture, ShaderProgram_heapBlocksThatDisagreeAreRejected)
{
    constexpr auto kStride = "descriptor_stride = 128";

    const auto vertex = heapShader(Shader::ShadingStage::eVertex,
                                   materialBlock("uniform", kStride, "vec4 tint;"),
                                   "gMaterials[pushData.index].tint");

    const auto differentMember =
        heapShader(Shader::ShadingStage::eFragment,
                   materialBlock("uniform", kStride, "vec3 tint; float pad;"),
                   "vec4(gMaterials[pushData.index].tint, gMaterials[pushData.index].pad)");
    try
    {
        std::ignore = makeProgram(*this, vertex, differentMember);
        ADD_FAILURE() << "Members declared differently did not throw.";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("\"tint\""), std::string_view::npos);
        EXPECT_NE(std::string_view{error.what()}.find("\"Material\""), std::string_view::npos);
    }

    // The same name at another offset, behind nothing the vertex stage declares.
    const auto movedMember =
        heapShader(Shader::ShadingStage::eFragment,
                   materialBlock("uniform", kStride, "layout(offset = 32) vec4 tint;"),
                   "gMaterials[pushData.index].tint");
    try
    {
        std::ignore = makeProgram(*this, vertex, movedMember);
        ADD_FAILURE() << "A member at two offsets did not throw.";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("\"tint\""), std::string_view::npos);
        EXPECT_NE(std::string_view{error.what()}.find("\"Material\""), std::string_view::npos);
    }

    const auto differentStorage = heapShader(Shader::ShadingStage::eFragment,
                                             materialBlock("buffer", kStride, "vec4 tint;"),
                                             "gMaterials[pushData.index].tint");
    try
    {
        std::ignore = makeProgram(*this, vertex, differentStorage);
        ADD_FAILURE() << "A block with two storage classes did not throw.";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(std::string_view{error.what()}.find("\"Material\""), std::string_view::npos);
    }
}

/**
 * The descriptor heap places resource i at byte i * 128: a heap block that a stage reads with a
 * stride of 64, or with none (the size of a descriptor), would read the wrong slot. The program
 * refuses it whichever stage reads it, and names the block.
 */
TEST_F(VulkanFixture, ShaderProgram_heapBlockStrideOtherThanSlotSizeIsRejected)
{
    const auto program = [this](std::string_view vertexStride, std::string_view fragmentStride)
    {
        return makeProgram(*this,
                           heapShader(Shader::ShadingStage::eVertex,
                                      materialBlock("uniform", vertexStride, "vec4 tint;"),
                                      "gMaterials[pushData.index].tint"),
                           heapShader(Shader::ShadingStage::eFragment,
                                      materialBlock("uniform", fragmentStride, "vec4 tint;"),
                                      "gMaterials[pushData.index].tint"));
    };
    constexpr auto kGood = "descriptor_stride = 128";

    EXPECT_NO_THROW(std::ignore = program(kGood, kGood));

    for (const auto* bad : {"descriptor_stride = 64", "descriptor_stride = 256", ""})
    {
        for (const bool inVertexStage : {true, false})
        {
            try
            {
                std::ignore = inVertexStage ? program(bad, kGood) : program(kGood, bad);
                ADD_FAILURE() << "The stride \"" << bad << "\" did not throw.";
            }
            catch (const std::runtime_error& error)
            {
                EXPECT_NE(std::string_view{error.what()}.find("\"Material\""),
                          std::string_view::npos)
                    << error.what();
                EXPECT_NE(std::string_view{error.what()}.find("128"), std::string_view::npos)
                    << error.what();
            }
        }
    }
}

/**
 * requireHeapBlock accepts the struct that has the block's layout, and reports the first
 * difference in offset order with the name of the block and of the member. Every kind of
 * difference is a test case: an offset, a type, a matrix stride, a member the struct lacks, a
 * member the block lacks, an array, and a block the program does not read.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(VulkanFixture, ShaderProgram_requireHeapBlockReportsTheFirstMismatch)
{
    constexpr auto kStride = "descriptor_stride = 128";

    const auto program = makeMaterialProgram(
        *this, materialBlock("uniform", kStride, gMaterialMembers), gMaterialRead);
    EXPECT_NO_THROW(program->requireHeapBlock("Material", materialLayout()));

    const auto message = [&program](const BlockLayout& layout)
    {
        return invalidArgumentOf(
            [&]
            {
                program->requireHeapBlock("Material", layout);
            });
    };
    const auto contains = [](const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    };

    // An offset: the struct has a float before the tint.
    struct Shifted
    {
        float pad;
        Vec4  tint;
        Mat4  basis;
        float rough;
    };

    const auto shifted = message(BlockLayout::of<Shifted>(
        {{"tint", &Shifted::tint}, {"basis", &Shifted::basis}, {"rough", &Shifted::rough}}));
    EXPECT_TRUE(contains(shifted, "\"tint\"")) << shifted;
    EXPECT_TRUE(contains(shifted, "\"Material\"")) << shifted;
    EXPECT_TRUE(contains(shifted, "offset 0 in the shader and 4 in C++")) << shifted;

    // A type: three components where the shader has four.
    struct Narrow
    {
        Vec3 tint;
        Mat4 basis;
    };

    const auto narrow =
        message(BlockLayout::of<Narrow>({{"tint", &Narrow::tint}, {"basis", &Narrow::basis}}));
    EXPECT_TRUE(contains(narrow, "\"tint\"")) << narrow;
    EXPECT_TRUE(contains(narrow, "has the type vec4 in the shader and vec3 in C++")) << narrow;

    // A member that the struct lacks: the block declares `rough`.
    struct Short
    {
        Vec4 tint;
        Mat4 basis;
    };

    const auto lacking =
        message(BlockLayout::of<Short>({{"tint", &Short::tint}, {"basis", &Short::basis}}));
    EXPECT_TRUE(contains(lacking, "\"rough\"")) << lacking;
    EXPECT_TRUE(contains(lacking, "C++ layout lacks")) << lacking;
    EXPECT_TRUE(lacking.starts_with("The block \"Material\" declares")) << lacking;

    // A member that the block lacks: unlike for push data, the struct must not have more.
    struct Long
    {
        Vec4  tint;
        Mat4  basis;
        float rough;
        float extra;
    };

    const auto extra = message(BlockLayout::of<Long>({{"tint", &Long::tint},
                                                      {"basis", &Long::basis},
                                                      {"rough", &Long::rough},
                                                      {"extra", &Long::extra}}));
    EXPECT_TRUE(contains(extra, "\"extra\"")) << extra;
    EXPECT_TRUE(contains(extra, "which the block \"Material\" lacks")) << extra;

    // A block the program does not read.
    const auto unread = invalidArgumentOf(
        [&]
        {
            program->requireHeapBlock("Light", materialLayout());
        });
    EXPECT_TRUE(contains(unread, "\"Light\"")) << unread;

    // A mat3 has 16 bytes between columns in the block, and 12 in a struct of 3-component columns.
    const auto mat3Program = makeMaterialProgram(*this,
                                                 materialBlock("uniform", kStride, "mat3 basis;"),
                                                 "vec4(gMaterials[pushData.index].basis[0], 1.0)");

    struct Packed
    {
        Matrix<Vec3, 3> basis;
    };

    const auto columns = invalidArgumentOf(
        [&]
        {
            mat3Program->requireHeapBlock("Material",
                                          BlockLayout::of<Packed>({{"basis", &Packed::basis}}));
        });
    EXPECT_TRUE(contains(columns, "\"basis\"")) << columns;
    EXPECT_TRUE(contains(columns, "16 bytes apart in the shader and 12 in C++")) << columns;

    // An array is not described by a layout: a float in its place is a difference, not a match.
    const auto arrayProgram = makeMaterialProgram(
        *this,
        materialBlock("uniform", kStride, "vec4 tint; float weights[3];"),
        "gMaterials[pushData.index].tint * gMaterials[pushData.index].weights[1]");

    struct Single
    {
        Vec4  tint;
        float weights;
    };

    const auto array = invalidArgumentOf(
        [&]
        {
            arrayProgram->requireHeapBlock(
                "Material",
                BlockLayout::of<Single>({{"tint", &Single::tint}, {"weights", &Single::weights}}));
        });
    EXPECT_TRUE(contains(array, "\"weights\"")) << array;
    EXPECT_TRUE(contains(array, "is an array")) << array;

    // A row-major matrix and a struct are not described by a layout either.
    const auto rowProgram =
        makeMaterialProgram(*this,
                            materialBlock("uniform", kStride, "layout(row_major) mat4 rows;"),
                            "gMaterials[pushData.index].rows[0]");

    struct Rows
    {
        Mat4 rows;
    };

    const auto rowMajor = invalidArgumentOf(
        [&]
        {
            rowProgram->requireHeapBlock("Material",
                                         BlockLayout::of<Rows>({{"rows", &Rows::rows}}));
        });
    EXPECT_TRUE(contains(rowMajor, "\"rows\"")) << rowMajor;
    EXPECT_TRUE(contains(rowMajor, "is a row_major matrix")) << rowMajor;

    const auto structProgram = makeMaterialProgram(
        *this,
        "struct Pair { uint a; uint b; };\n" + materialBlock("uniform", kStride, "Pair pair;"),
        "vec4(gMaterials[pushData.index].pair.a)");

    struct Flat
    {
        std::uint32_t pair;
    };

    const auto structure = invalidArgumentOf(
        [&]
        {
            structProgram->requireHeapBlock("Material",
                                            BlockLayout::of<Flat>({{"pair", &Flat::pair}}));
        });
    EXPECT_TRUE(contains(structure, "\"pair\"")) << structure;
    EXPECT_TRUE(contains(structure, "is a struct")) << structure;
}

/**
 * requirePushBlock lets the struct have members that no stage declares (stages declare subsets),
 * and refuses a struct that differs from a push member a stage does declare.
 */
TEST_F(VulkanFixture, ShaderProgram_requirePushBlockChecksTheDeclaredMembers)
{
    const auto program = makeProgram(*this, gVertexShaderPushRead, gMinimalFragmentShader);

    struct Push
    {
        Mat4          m;
        std::uint32_t a;
    };

    EXPECT_NO_THROW(
        program->requirePushBlock(BlockLayout::of<Push>({{"m", &Push::m}, {"a", &Push::a}})));

    struct Larger
    {
        Mat4          m;
        std::uint32_t a;
        std::uint32_t spare;
    };

    EXPECT_NO_THROW(program->requirePushBlock(BlockLayout::of<Larger>(
        {{"m", &Larger::m}, {"a", &Larger::a}, {"spare", &Larger::spare}})));

    struct Signed
    {
        Mat4         m;
        std::int32_t a;
    };

    const auto type = invalidArgumentOf(
        [&]
        {
            program->requirePushBlock(
                BlockLayout::of<Signed>({{"m", &Signed::m}, {"a", &Signed::a}}));
        });
    EXPECT_NE(type.find("\"a\""), std::string::npos) << type;
    EXPECT_NE(type.find("has the type uint in the shader and int in C++"), std::string::npos)
        << type;

    struct Reordered
    {
        std::uint32_t a;
        Mat4          m;
    };

    const auto offset = invalidArgumentOf(
        [&]
        {
            program->requirePushBlock(
                BlockLayout::of<Reordered>({{"m", &Reordered::m}, {"a", &Reordered::a}}));
        });
    EXPECT_NE(offset.find("\"m\""), std::string::npos) << offset;

    struct OnlyMatrix
    {
        Mat4 m;
    };

    const auto lacking = invalidArgumentOf(
        [&]
        {
            program->requirePushBlock(BlockLayout::of<OnlyMatrix>({{"m", &OnlyMatrix::m}}));
        });
    EXPECT_NE(lacking.find("\"a\""), std::string::npos) << lacking;
    EXPECT_TRUE(lacking.starts_with("The push block declares")) << lacking;

    // A program with no push block has nothing to differ from.
    EXPECT_NO_THROW(makeProgram(*this, gMinimalVertexShader, gMinimalFragmentShader)
                        ->requirePushBlock(BlockLayout::of<Push>({{"m", &Push::m}})));
}

/**
 * The overlap test does not depend on which stage declares the outer range, and a push member of
 * one name at two offsets is refused too, though its two places do not overlap.
 */
TEST_F(VulkanFixture, ShaderProgram_overlappingPushConstantsThrowInEitherStageOrder)
{
    const auto program = [this](const std::string& vertex, const std::string& fragment)
    {
        return ShaderProgram::create(
            {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, vertex)},
             .fragment = {.shader = createShader(Shader::ShadingStage::eFragment, fragment)}});
    };

    // What the merge says, not only that something throws: a shader that does not compile throws
    // a std::runtime_error too.
    const auto messageOf = [&program](const std::string& vertex, const std::string& fragment)
    {
        try
        {
            std::ignore = program(vertex, fragment);
        }
        catch (const Shader::CompilationError& error)
        {
            ADD_FAILURE() << "A shader did not compile: " << error.what();
        }
        catch (const std::runtime_error& error)
        {
            return std::string{error.what()};
        }
        ADD_FAILURE() << "The program was made.";

        return std::string{};
    };
    const auto contains = [](const std::string& text, std::string_view part)
    {
        return text.find(part) != std::string::npos;
    };

    for (const auto& message :
         {messageOf(gVertexShaderPushMatrix, gFragmentShaderPushInsideMatrix),
          messageOf(gVertexShaderPushInsideMatrix, gFragmentShaderPushMatrix)})
    {
        EXPECT_TRUE(contains(message, "over the same bytes")) << message;
        EXPECT_TRUE(contains(message, "push member")) << message;
    }

    for (const auto& message :
         {messageOf(gVertexShaderPushUnread, gFragmentShaderPushAtOtherOffset),
          messageOf(gVertexShaderPushAtOtherOffset, gFragmentShaderPushUnread)})
    {
        EXPECT_TRUE(contains(message, "the push member \"a\" differently")) << message;
    }
}

// ============================================================
// Lifetime tests (VulkanFixture)
// ============================================================

/**
 * The pipeline retains the device through its DeviceChild base, which is destroyed after the
 * pipeline handle. Its ShaderProgram member also holds the device, through the shaders, but is
 * destroyed before the handle, so only the base keeps the device alive until vkDestroyPipeline:
 * without it the layer reports the pipeline as still alive at vkDestroyDevice.
 */
TEST_F(VulkanValidationFixture, GraphicsPipeline_keepsDeviceAlive)
{
    ShaderProgramPtr program = ShaderProgram::create(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    GraphicsPipelinePtr pipeline =
        mDevice->createGraphicsPipeline(makeMinimalPipelineParameters(program));
    program.reset();
    const auto released = releaseDevice();

    // Everything made inside is gone when the lambda returns, so the pipeline is the last holder.
    expectDependentHoldsDevice(released,
                               pipeline,
                               [&pipeline](GraphicsPipeline&)
                               {
                                   const auto&     device = pipeline->device();
                                   auto            pool   = device->createCommandBufferPool();
                                   RenderTargetPtr target = createColorTarget(
                                       *device, 16u, vk::Format::eR32G32B32A32Sfloat);
                                   auto recorder = pool->record();

                                   recorder.setImageBarrier(target, toColorAttachmentBarrier());
                                   {
                                       auto pass =
                                           recorder.beginRendering({{.attachment = target}});
                                       pass.bindPipeline(pipeline);
                                   }
                                   auto executable = recorder.finish();
                               });
}

/**
 * The command buffer retains a pipeline once, however often it binds it: retention is a set, so
 * a draw-heavy frame does not grow the command buffer's retained list.
 */
TEST_F(VulkanFixture, CommandBufferRecorder_bindingOnePipelineRepeatedlyRetainsItOnce)
{
    ShaderProgramPtr program = ShaderProgram::create(
        {.vertex   = {.shader = createShader(Shader::ShadingStage::eVertex, gMinimalVertexShader)},
         .fragment = {.shader =
                          createShader(Shader::ShadingStage::eFragment, gMinimalFragmentShader)}});
    GraphicsPipelinePtr pipeline =
        mDevice->createGraphicsPipeline(makeMinimalPipelineParameters(program));
    RenderTargetPtr target = createColorTarget(*mDevice, 16u, vk::Format::eR32G32B32A32Sfloat);

    auto pool     = mDevice->createCommandBufferPool();
    auto recorder = pool->record();
    {
        auto pass = recorder.beginRendering({{.attachment = target}});
        for (int i = 0; i < 1000; ++i)
        {
            pass.bindPipeline(pipeline);
        }
    }

    // One reference for the test, one for the command buffer.
    EXPECT_EQ(pipeline.use_count(), 2);
}

namespace
{
struct Mixed
{
    Vec4          a;
    float         b;
    Vec3          c;
    std::uint32_t d;
    Mat4          e;
    std::int32_t  f;
};

struct WithBool
{
    bool flag;
};
} // namespace

/**
 * BlockLayout reads the offset of each member from the struct itself, takes its GLSL type from the
 * C++ type, and sorts by offset whatever the order of the entries.
 */
TEST(BlockLayout, readsOffsetsAndTypesFromTheStruct)
{
    using Type = Shader::Reflection::BaseType::Type;

    const auto layout = BlockLayout::of<Mixed>({{"f", &Mixed::f},
                                                {"c", &Mixed::c},
                                                {"a", &Mixed::a},
                                                {"e", &Mixed::e},
                                                {"d", &Mixed::d},
                                                {"b", &Mixed::b}});

    ASSERT_EQ(layout.members.size(), 6u);
    const std::array<std::pair<std::string_view, std::uint32_t>, 6> expected{
        {{"a", offsetof(Mixed, a)},
         {"b", offsetof(Mixed, b)},
         {"c", offsetof(Mixed, c)},
         {"d", offsetof(Mixed, d)},
         {"e", offsetof(Mixed, e)},
         {"f", offsetof(Mixed, f)}}};
    for (const auto& [member, name] : std::views::zip(layout.members, expected))
    {
        EXPECT_EQ(member.name, name.first);
        EXPECT_EQ(member.offset, name.second) << member.name;
    }

    EXPECT_EQ(layout.members[0].type.type,
              (Shader::Reflection::BaseType{.type = Type::eFloat, .components = 4, .columns = 1}));
    EXPECT_EQ(layout.members[0].type.matrixStride, 0u);
    EXPECT_EQ(layout.members[3].type.type,
              (Shader::Reflection::BaseType{.type = Type::eUInt, .components = 1, .columns = 1}));
    EXPECT_EQ(layout.members[4].type.type,
              (Shader::Reflection::BaseType{.type = Type::eFloat, .components = 4, .columns = 4}));
    EXPECT_EQ(layout.members[4].type.matrixStride, sizeof(Vec4));
    EXPECT_EQ(layout.members[5].type.type,
              (Shader::Reflection::BaseType{.type = Type::eInt, .components = 1, .columns = 1}));
}

// A type that is no scalar, vector or matrix of 32-bit numbers has no block member type.
static_assert(BlockMemberCompatible<float>);
static_assert(BlockMemberCompatible<Vec4>);
static_assert(BlockMemberCompatible<Mat4>);
static_assert(!BlockMemberCompatible<bool>);
static_assert(!BlockMemberCompatible<std::int64_t>);
static_assert(!BlockMemberCompatible<std::array<float, 4>>);
static_assert(
    !std::is_constructible_v<BlockLayout::Entry<WithBool>, std::string, bool WithBool::*>);

} // namespace VOG::Tests