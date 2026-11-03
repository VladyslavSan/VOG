#include "VOG/Engine/Renderer/CameraView.hpp"

#include <VOG/Math/Projections.hpp>

namespace VOG::Engine
{
Math::Matrix4x4f
CameraView::viewProjection(Extent extent) const
{
    const Math::Matrix4x4f view = glm::inverse(
        Math::calculateCameraMatrix({.position = position, .direction = direction, .up = up}));
    const Math::Matrix4x4f projection = Math::orthographicProjection(
        {0.0f, 0.0f},
        {static_cast<float>(extent.width), static_cast<float>(extent.height)},
        nearPlane,
        farPlane);

    // Vulkan clip space has +y down the framebuffer. World +Y, the camera's up, must be up the
    // screen, so clip y is negated here and neither the viewport nor the pipeline flips. That
    // reverses the winding in the framebuffer, which is harmless while PipelineCache culls nothing.
    const Math::Matrix4x4f flipY =
        glm::scale(Math::Matrix4x4f{1.0f}, Math::Vector3f{1.0f, -1.0f, 1.0f});

    return flipY * projection * view;
}
} // namespace VOG::Engine
