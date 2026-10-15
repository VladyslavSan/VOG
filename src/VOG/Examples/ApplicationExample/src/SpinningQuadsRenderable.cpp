#include "SpinningQuadsRenderable.hpp"

#include <VOG/Graphics/ShaderProgramCache.hpp>
#include <VOG/Graphics/Vulkan/Buffer.hpp>
#include <VOG/Graphics/Vulkan/Device.hpp>
#include <VOG/Graphics/Vulkan/GraphicsPipeline.hpp>
#include <VOG/Graphics/Vulkan/ShaderProgram.hpp>
#include <VOG/Math/Math.hpp>
#include <VOG/Math/Projections.hpp>

#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

using namespace VOG::Graphics::Vulkan;
using namespace VOG::Math;

namespace
{
struct VertexData
{
    Vector2f position;
    Vector4f color;
};

// clang-format off
constexpr VertexData gVertexData[] = {
  // Red bottom left square
  {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
  {{-1.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
  {{ 0.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
  {{ 0.0,  0.0}, {1.0, 0.0, 0.0, 1.0}},
  {{ 0.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
  {{-1.0, -1.0}, {1.0, 0.0, 0.0, 1.0}},
  // Green top right square
  {{ 0.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 0.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 1.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 0.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 1.0,  1.0}, {0.0, 1.0, 0.0, 1.0}},
  {{ 1.0,  0.0}, {0.0, 1.0, 0.0, 1.0}},
};
// clang-format on

constexpr std::size_t gVertexDataSize = std::size(gVertexData) * sizeof(VertexData);

/** Rate at which the quads swing back and forth, in radians per second. */
constexpr float gRotationRate = 1.6f;

constexpr float gQuadHalfSizePixels = 300.0f;
constexpr float gQuadDistance       = 20.0f;
constexpr float gNearPlane          = 1.0f;
constexpr float gFarPlane           = 200.0f;
constexpr float gMaxRotationDegrees = 90.0f;
} // namespace

void
SpinningQuadsRenderable::prepare(const VOG::Engine::ResourceContext& resourceContext)
{
    const auto& device = resourceContext.device;

    mVertexBuffer = device->createBuffer(
        {.size = gVertexDataSize, .usage = vk::BufferUsageFlagBits::eVertexBuffer},
        {.flags = AllocationFlag::eHostSequentialWrite});
    {
        auto mapping = mVertexBuffer->mapForWrite();
        std::memcpy(mapping.data, gVertexData, gVertexDataSize);
    }

    mVertexCount = static_cast<std::uint32_t>(std::size(gVertexData));

    const auto program = resourceContext.shaderProgramCache->get("WorldSpace");

    constexpr auto colorWriteMask =
        ColorComponent::eR | ColorComponent::eG | ColorComponent::eB | ColorComponent::eA;

    mPipeline = device->createGraphicsPipeline(GraphicsPipeline::Parameters{
        .shading       = program,
        .vertexLayout  = {.bindingDescription =
                              {
                                  {.binding   = 0u,
                                   .stride    = sizeof(VertexData),
                                   .inputRate = vk::VertexInputRate::eVertex},
                              },
                          .attributeDescription = {{.location = 0u,
                                                    .binding  = 0u,
                                                    .format   = vk::Format::eR32G32Sfloat,
                                                    .offset   = 0u},
                                                   {.location = 1u,
                                                    .binding  = 0u,
                                                    .format   = vk::Format::eR32G32B32A32Sfloat,
                                                    .offset   = offsetof(VertexData, color)}}},
        .rasterizer    = {.cullMode = CullMode::eNone},
        .viewportState = {.viewportCount = 1u, .scissorCount = 1u},
        .depthStencil  = {.depthTestEnable = 0u},
        .blending      = {.attachments = {{.blendEnable = 0u, .colorWriteMask = colorWriteMask}}},
        .multisample   = {},
        .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor},
        .renderingDescription = {
            .colorAttachmentFormats = {resourceContext.colorAttachmentFormat}}});
}

void
SpinningQuadsRenderable::collect(const VOG::Engine::FrameContext&      frameContext,
                                 std::vector<VOG::Engine::RenderItem>& renderItems)
{
    const float timeFactor = std::sin(static_cast<float>(frameContext.timeSeconds) * gRotationRate);

    const Vector3f objectPosition = {0.0, 0.0, gQuadDistance};

    const auto model =
        glm::translate(objectPosition) *
        glm::scale(Vector3f{gQuadHalfSizePixels, gQuadHalfSizePixels, 1.0}) *
        glm::rotate(glm::radians(timeFactor * gMaxRotationDegrees), Vector3f{0.0, 0.0, 1.0});

    const Vector3f cameraPosition  = {0.0, 0.0, 0.0};
    const Vector3f cameraDirection = glm::normalize(objectPosition - cameraPosition);
    const Vector3f cameraUp        = {0.0, 1.0, 0.0};

    const Matrix4x4f cameraMatrix =
        calculateCameraMatrix(cameraPosition, cameraDirection, cameraUp);

    const Matrix4x4f viewMatrix = glm::inverse(cameraMatrix);
    const Matrix4x4f projection =
        orthographicProjection({0.0, 0.0},
                               {static_cast<float>(frameContext.extent.width),
                                static_cast<float>(frameContext.extent.height)},
                               gNearPlane,
                               gFarPlane);

    const Matrix4x4f mvp = projection * viewMatrix * model;

    VOG::Engine::RenderItem item{
        .pipeline     = mPipeline,
        .vertexBuffer = mVertexBuffer,
        .vertexCount  = mVertexCount,
    };
    item.setPushConstants(mvp);

    renderItems.push_back(std::move(item));
}
