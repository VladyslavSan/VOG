#include "SpinningQuadsRenderable.hpp"

#include <VOG/Engine/Renderer/Renderer.hpp>
#include <VOG/Math/Math.hpp>

#include <glm/gtx/transform.hpp>

#include <cmath>
#include <cstddef>
#include <iterator>
#include <span>

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

/** Rate at which the quads swing back and forth, in radians per second. */
constexpr float gRotationRate = 1.6f;

constexpr float gQuadHalfSizePixels = 300.0f;
constexpr float gQuadDistance       = 20.0f;
constexpr float gMaxRotationDegrees = 90.0f;
} // namespace

SpinningQuadsRenderable::SpinningQuadsRenderable(VOG::Engine::Renderer& renderer)
    : mMesh{renderer.createMesh({
          .vertices   = std::as_bytes(std::span{gVertexData}),
          .stride     = sizeof(VertexData),
          .attributes = {{VOG::Engine::VertexFormat::eFloat2, 0u},
                         {VOG::Engine::VertexFormat::eFloat4, offsetof(VertexData, color)}},
      })}
    , mMaterial{renderer.createMaterial({.shader = "WorldSpace"})}
{
}

void
SpinningQuadsRenderable::onDraw(const VOG::Engine::FrameContext& frameContext,
                                VOG::Engine::RenderQueue&        queue)
{
    const float timeFactor = std::sin(static_cast<float>(frameContext.timeSeconds) * gRotationRate);

    const auto model =
        glm::translate(Vector3f{0.0, 0.0, gQuadDistance}) *
        glm::scale(Vector3f{gQuadHalfSizePixels, gQuadHalfSizePixels, 1.0}) *
        glm::rotate(glm::radians(timeFactor * gMaxRotationDegrees), Vector3f{0.0, 0.0, 1.0});

    queue.submit(mMesh, mMaterial, model);
}
