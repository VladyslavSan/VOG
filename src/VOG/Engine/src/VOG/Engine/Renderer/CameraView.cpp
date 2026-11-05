#include "VOG/Engine/Renderer/CameraView.hpp"

#include <VOG/Math/Projections.hpp>

namespace VOG::Engine
{
namespace
{
template <class... Visitors>
struct Overloaded : Visitors...
{
    using Visitors::operator()...;
};
} // namespace

Math::Matrix4x4f
CameraView::view() const
{
    return glm::inverse(
        Math::calculateCameraMatrix({.position = position, .direction = direction, .up = up}));
}

Math::Matrix4x4f
CameraView::projectionMatrix(Extent extent) const
{
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    const Math::Matrix4x4f projected =
        std::visit(Overloaded{[aspect](const Orthographic& orthographic)
                              {
                                  return Math::orthographicProjection(
                                      {0.0f, 0.0f},
                                      {orthographic.height * aspect, orthographic.height},
                                      orthographic.nearPlane,
                                      orthographic.farPlane);
                              },
                              [aspect](const Perspective& perspective)
                              {
                                  return glm::perspective(perspective.verticalFovRadians,
                                                          aspect,
                                                          perspective.nearPlane,
                                                          perspective.farPlane);
                              }},
                   projection);

    // Vulkan clip space has +y down the framebuffer. World +Y, the camera's up, must be up the
    // screen, so clip y is negated here and neither the viewport nor the pipeline flips. The
    // winding that the framebuffer sees follows from it: RendererTest's cull test pins the front
    // face that RenderState uses.
    const Math::Matrix4x4f flipY =
        glm::scale(Math::Matrix4x4f{1.0f}, Math::Vector3f{1.0f, -1.0f, 1.0f});

    return flipY * projected;
}

Math::Matrix4x4f
CameraView::viewProjection(Extent extent) const
{
    return projectionMatrix(extent) * view();
}
} // namespace VOG::Engine
