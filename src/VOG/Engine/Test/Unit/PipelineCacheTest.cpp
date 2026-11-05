#include "VOG/Engine/Renderer/PipelineCache.hpp"

#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include "VOG/Engine/Renderer/ShaderLibrary.hpp"
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/Instance.hpp>

#include <gtest/gtest.h>

#include "TestShaders.hpp"

using namespace VOG::Engine;

namespace VOG::Tests
{
namespace
{
class PipelineCacheTest : public testing::Test
{
protected:
    Detail::MaterialData
    material(const char* shader) const
    {
        return {.shader = mShaders.get(shader)};
    }

    /** The cache keeps the pipeline, so the pointer stays valid. */
    const Graphics::Vulkan::GraphicsPipeline*
    pipeline(const Detail::MeshData&     mesh,
             const Detail::MaterialData& material,
             vk::Format                  colorFormat)
    {
        return mCache.get(mesh, material, colorFormat).get();
    }

    Graphics::Vulkan::DevicePtr mDevice =
        Graphics::Vulkan::Instance::create(
            {.appName = "VOG PipelineCache Test", .engineName = "Test"})
            ->makeDevice();
    ShaderLibrary mShaders{mDevice, Tests::sharedTestShaderPath()};
    PipelineCache mCache{mDevice};

    const Detail::MeshData mQuad{
        .stride = 24u, .attributes = {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat4, 8u}}};
};
} // namespace

TEST_F(PipelineCacheTest, sameLayoutProgramAndFormatShareAPipeline)
{
    const Detail::MaterialData worldSpace = material("WorldSpace");

    const auto* first = pipeline(mQuad, worldSpace, vk::Format::eB8G8R8A8Unorm);

    EXPECT_EQ(pipeline(mQuad, worldSpace, vk::Format::eB8G8R8A8Unorm), first);
    // A material with the same program, made again.
    EXPECT_EQ(pipeline(mQuad, material("WorldSpace"), vk::Format::eB8G8R8A8Unorm), first);
}

TEST_F(PipelineCacheTest, differentFormatLayoutOrProgramBuildsAnotherPipeline)
{
    const Detail::MaterialData worldSpace = material("WorldSpace");

    const auto* first = pipeline(mQuad, worldSpace, vk::Format::eB8G8R8A8Unorm);

    EXPECT_NE(pipeline(mQuad, worldSpace, vk::Format::eR8G8B8A8Unorm), first);
    EXPECT_NE(pipeline(mQuad, material("ScreenSpacePositionColor"), vk::Format::eB8G8R8A8Unorm),
              first);

    // Each field of the vertex layout alone.
    const Detail::MeshData wider{
        .stride = 32u, .attributes = {{VertexFormat::eFloat2, 0u}, {VertexFormat::eFloat4, 8u}}};
    EXPECT_NE(pipeline(wider, worldSpace, vk::Format::eB8G8R8A8Unorm), first);

    const Detail::MeshData swapped{
        .stride = 24u, .attributes = {{VertexFormat::eFloat4, 0u}, {VertexFormat::eFloat2, 16u}}};
    EXPECT_NE(pipeline(swapped, worldSpace, vk::Format::eB8G8R8A8Unorm), first);
}
} // namespace VOG::Tests
