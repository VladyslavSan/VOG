#include "VOG/Engine/Renderer/PipelineTable.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <gtest/gtest.h>

#include <memory>

#include "TestShaders.hpp"

using namespace VOG::Engine;

namespace VOG::Engine
{
/** Test-only window into the table, befriended by PipelineTable. */
struct PipelineTableTestAccess
{
    static std::size_t
    size(const PipelineTable& table)
    {
        return table.mPipelines.size();
    }
};
} // namespace VOG::Engine

namespace VOG::Tests
{
namespace
{
constexpr std::uint32_t gMoreLayouts        = 64u;
constexpr std::uint32_t gFirstOtherLayout   = 10u;
constexpr std::uint32_t gOtherProgramOffset = 100u;

class PipelineTableTest : public testing::Test
{
protected:
    Detail::MaterialData
    material(const char* shader) const
    {
        return {.shader = mShaders.get(shader)};
    }

    Graphics::Vulkan::DevicePtr mDevice =
        Graphics::Vulkan::Instance::create(
            {.appName = "VOG PipelineTable Test", .engineName = "Test"})
            ->makeDevice();
    ShaderLibrary mShaders{mDevice, Tests::sharedTestShaderPath()};
    PipelineTable mTable{mDevice};

    static constexpr vk::Format kFormat = vk::Format::eB8G8R8A8Unorm;

    const Detail::MeshData mQuad{
        .layout   = vertexLayout(24u, {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat4, 8u}}),
        .layoutId = 1u};
};
} // namespace

TEST_F(PipelineTableTest, sameLayoutProgramAndFormatShareAPipeline)
{
    const Detail::MaterialData worldSpace = material("WorldSpace");

    const std::uint32_t first = mTable.resolve(mQuad, worldSpace, kFormat);

    EXPECT_EQ(mTable.resolve(mQuad, worldSpace, kFormat), first);
    // A material with the same program, made again.
    EXPECT_EQ(mTable.resolve(mQuad, material("WorldSpace"), kFormat), first);
    EXPECT_EQ(PipelineTableTestAccess::size(mTable), 1u);
}

TEST_F(PipelineTableTest, differentFormatLayoutOrProgramBuildsAnotherPipeline)
{
    const Detail::MaterialData worldSpace = material("WorldSpace");

    const std::uint32_t first = mTable.resolve(mQuad, worldSpace, kFormat);

    EXPECT_NE(mTable.resolve(mQuad, worldSpace, vk::Format::eR8G8B8A8Unorm), first);
    EXPECT_NE(mTable.resolve(mQuad, material("ScreenSpacePositionColor"), kFormat), first);

    // Another layout id with the layout of the first mesh: the id is the key, and the layout is
    // what the pipeline is built from.
    Detail::MeshData wider = mQuad;
    wider.layoutId         = 2u;
    EXPECT_NE(mTable.resolve(wider, worldSpace, kFormat), first);
}

/**
 * The key is the id of the program, never its address, so a program that is made again, with a
 * new id, is another pipeline even where the old object is gone; and an object that keeps the id
 * is the same one. The indices stay valid when later calls add pipelines: the draws of a frame
 * hold them while the table grows.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST_F(PipelineTableTest, theKeyIsTheProgramIdAndAnIndexStaysValid)
{
    const Detail::MaterialData worldSpace = material("WorldSpace");

    const std::uint32_t first = mTable.resolve(mQuad, worldSpace, kFormat);
    const auto&         held  = mTable.at(first);
    const auto*         raw   = held.get();

    auto sameId    = std::make_shared<Detail::ShaderInfo>(*worldSpace.shader);
    auto anotherId = std::make_shared<Detail::ShaderInfo>(*worldSpace.shader);
    anotherId->programId += gOtherProgramOffset;
    EXPECT_EQ(mTable.resolve(mQuad, {.shader = sameId}, kFormat), first);
    EXPECT_NE(mTable.resolve(mQuad, {.shader = anotherId}, kFormat), first);

    // Many more pipelines: the first one has not moved.
    for (std::uint32_t id = 1u; id <= gMoreLayouts; ++id)
    {
        Detail::MeshData other = mQuad;
        other.layoutId         = gFirstOtherLayout + id;
        std::ignore            = mTable.resolve(other, worldSpace, kFormat);
    }
    EXPECT_EQ(mTable.at(first).get(), raw);
    EXPECT_EQ(&mTable.at(first), &held);
}
} // namespace VOG::Tests
