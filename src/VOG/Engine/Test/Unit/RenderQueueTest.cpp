#include "VOG/Engine/Renderer/DrawCheck.hpp"
#include "VOG/Engine/Renderer/ShaderDefinition.hpp"
#include <VOG/Graphics/Vulkan/Shader.hpp>

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "HandleTestAccess.hpp"
#include "RenderQueueTestAccess.hpp"

using namespace VOG::Engine;

namespace VOG::Engine::Detail
{
/** Test-only window into the check, befriended by DrawCheck. */
struct DrawCheckTestAccess
{
    static std::size_t
    verdictCount(const DrawCheck& check)
    {
        return check.mVerdicts.size();
    }
};
} // namespace VOG::Engine::Detail

namespace VOG::Tests
{
namespace
{
using Detail::VertexInput;
using Graphics::Vulkan::Shader;
using BaseType = Shader::Reflection::BaseType;

/** A float vector of @p components, as a shader declares an input: vec2, vec3, vec4. */
constexpr BaseType
floatVector(std::uint8_t components)
{
    return {.type = BaseType::Type::eFloat, .components = components, .columns = 1u};
}

/** The layout of the quad that most tests draw: two floats of position, two of uv. */
constexpr VertexLayout gQuadLayout =
    vertexLayout(16u, {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat2, 8u}});

/** A shader that reads the position (vec3, fed two floats) at 0 and the uv at 1. */
std::vector<VertexInput>
quadInputs()
{
    return {{.location = 0u, .locationCount = 1u, .name = "inPosition", .type = floatVector(3u)},
            {.location = 1u, .locationCount = 1u, .name = "inUv", .type = floatVector(2u)}};
}

/** A mesh of the quad layout, which has the id @p layoutId. */
Mesh
quad(std::uint32_t layoutId = 1u)
{
    return HandleTestAccess::makeMesh(gQuadLayout, layoutId);
}
} // namespace

// NOLINTBEGIN(readability-magic-numbers): the numbers only tell cameras apart.
TEST(RenderQueueTest, lastSetCameraWins)
{
    Detail::DrawCheck check;
    RenderQueue       queue = RenderQueueTestAccess::make(check);

    queue.setCamera(CameraView{.clearColor = {2.0f, 0.0f, 0.0f, 1.0f}});
    queue.setCamera(CameraView{.clearColor = {3.0f, 0.0f, 0.0f, 1.0f}});

    EXPECT_EQ(RenderQueueTestAccess::camera(queue).clearColor.x, 3.0f);
}

TEST(RenderQueueTest, defaultCameraIsDefaultView)
{
    Detail::DrawCheck check;
    const RenderQueue queue = RenderQueueTestAccess::make(check);
    const CameraView  defaultView;

    const CameraView& camera = RenderQueueTestAccess::camera(queue);
    EXPECT_EQ(camera.position, defaultView.position);
    EXPECT_EQ(camera.direction, defaultView.direction);
    EXPECT_EQ(camera.up, defaultView.up);
    EXPECT_EQ(camera.projection, defaultView.projection);
    EXPECT_EQ(camera.clearColor, defaultView.clearColor);
    EXPECT_TRUE(RenderQueueTestAccess::draws(queue).empty());
}

/**
 * A mesh that lacks an input of the shader is a mistake of the caller of submit(), and that call
 * gets the DrawError: it names the mesh with its layout, the shader and the first input that is
 * missing. Nothing is added to the queue. Location 1 is there; location 2 is not.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RenderQueueTest, drawErrorNamesMeshShaderAndMissingInput)
{
    Detail::DrawCheck check;
    RenderQueue       queue  = RenderQueueTestAccess::make(check);
    auto              inputs = quadInputs();
    inputs.push_back(
        {.location = 2u, .locationCount = 1u, .name = "inNormal", .type = floatVector(3u)});
    const Material lit = HandleTestAccess::makeMaterial(inputs);

    try
    {
        queue.submit(quad(), lit, Math::Matrix4x4f{1.0f});
        FAIL() << "submit() did not throw.";
    }
    catch (const DrawError& error)
    {
        EXPECT_EQ(std::string_view{error.what()},
                  "mesh \"mesh\" (float2 @0, float2 @8) does not provide location 2 (vec3 "
                  "inNormal) of shader \"shader\"");
    }
    EXPECT_TRUE(RenderQueueTestAccess::draws(queue).empty());
}

/** A matrix input covers one location per column; the first column that the layout lacks is the
 *  one named, and an attribute of another numeric type than the input's is named for its type. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RenderQueueTest, drawErrorNamesTheLocationOfAMatrixAndTheTypeOfAWrongAttribute)
{
    Detail::DrawCheck check;
    RenderQueue       queue = RenderQueueTestAccess::make(check);

    // mat3 at location 1: columns 1, 2 and 3. The quad has location 1; location 2 is the first it
    // lacks.
    const Material matrix = HandleTestAccess::makeMaterial(
        {{.location      = 1u,
          .locationCount = 3u,
          .name          = "inBasis",
          .type          = {.type = BaseType::Type::eFloat, .components = 3u, .columns = 3u}}});
    try
    {
        queue.submit(quad(), matrix, Math::Matrix4x4f{1.0f});
        FAIL() << "submit() did not throw.";
    }
    catch (const DrawError& error)
    {
        EXPECT_EQ(std::string_view{error.what()},
                  "mesh \"mesh\" (float2 @0, float2 @8) does not provide location 2 (mat3 "
                  "inBasis) of shader \"shader\"");
    }

    // An integer input cannot be fed floats.
    const Material integer = HandleTestAccess::makeMaterial(
        {{.location      = 1u,
          .locationCount = 1u,
          .name          = "inId",
          .type          = {.type = BaseType::Type::eInt, .components = 2u, .columns = 1u}}},
        2u);
    try
    {
        queue.submit(quad(), integer, Math::Matrix4x4f{1.0f});
        FAIL() << "submit() did not throw.";
    }
    catch (const DrawError& error)
    {
        EXPECT_EQ(std::string_view{error.what()},
                  "mesh \"mesh\" (float2 @0, float2 @8) provides float2 at location 1, which "
                  "cannot feed (ivec2 inId) of shader \"shader\"");
    }
}

/** A mesh with fewer components than the input, or with more attributes than the shader reads, is
 *  drawn: Vulkan fills the components, and an attribute that nothing reads costs nothing. An empty
 *  mesh or material is no draw and no error. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RenderQueueTest, compatibleAndEmptyDrawsAreAcceptedOrDroppedWithoutAnError)
{
    Detail::DrawCheck check;
    RenderQueue       queue = RenderQueueTestAccess::make(check);

    const Material material = HandleTestAccess::makeMaterial(quadInputs());
    EXPECT_NO_THROW(queue.submit(quad(), material, Math::Matrix4x4f{1.0f}));
    EXPECT_EQ(RenderQueueTestAccess::draws(queue).size(), 1u);

    // Empty handles never reach the check, though a bad pair would fail it.
    const Material needsNormal = HandleTestAccess::makeMaterial(
        {{.location = 2u, .locationCount = 1u, .name = "inNormal", .type = floatVector(3u)}}, 2u);
    EXPECT_NO_THROW(queue.submit({}, needsNormal, Math::Matrix4x4f{1.0f}));
    EXPECT_NO_THROW(queue.submit(quad(), {}, Math::Matrix4x4f{1.0f}));
    EXPECT_NO_THROW(queue.submit({}, {}, Math::Matrix4x4f{1.0f}));
    EXPECT_EQ(RenderQueueTestAccess::draws(queue).size(), 1u);
}

/**
 * The verdict is kept for each pair of layout and program, a failure too: a caller that catches
 * the DrawError and goes on gets it again for the next object with that pair, and the check is not
 * run again. A pair that differs in either id is another entry.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(RenderQueueTest, theVerdictIsKeptForEachPairAndAFailureToo)
{
    Detail::DrawCheck check;
    RenderQueue       queue = RenderQueueTestAccess::make(check);

    const Material good = HandleTestAccess::makeMaterial(quadInputs(), 1u);
    const Material bad  = HandleTestAccess::makeMaterial(
        {{.location = 2u, .locationCount = 1u, .name = "inNormal", .type = floatVector(3u)}}, 2u);

    std::vector<std::string> messages;
    for (int draw = 0; draw < 2; ++draw)
    {
        try
        {
            queue.submit(quad(), bad, Math::Matrix4x4f{1.0f});
            FAIL() << "submit() did not throw.";
        }
        catch (const DrawError& error)
        {
            messages.emplace_back(error.what());
        }
    }
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_TRUE(std::string_view{messages[0]}.starts_with("mesh "));
    EXPECT_EQ(messages[0], messages[1]);
    EXPECT_EQ(Detail::DrawCheckTestAccess::verdictCount(check), 1u);

    for (int draw = 0; draw < 3; ++draw)
    {
        queue.submit(quad(), good, Math::Matrix4x4f{1.0f});
    }
    EXPECT_EQ(Detail::DrawCheckTestAccess::verdictCount(check), 2u);

    // Another layout of the same program is another pair.
    queue.submit(quad(2u), good, Math::Matrix4x4f{1.0f});
    EXPECT_EQ(Detail::DrawCheckTestAccess::verdictCount(check), 3u);
}

// NOLINTEND(readability-magic-numbers)
} // namespace VOG::Tests
