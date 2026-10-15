#include "VOG/Engine/Renderer/Camera.hpp"

#include <VOG/Math/Projections.hpp>

namespace VOG::Engine
{
Math::Matrix4x4f
Camera::viewProjection(Extent extent) const
{
    const Math::Matrix4x4f view =
        glm::inverse(Math::calculateCameraMatrix(position, direction, up));
    const Math::Matrix4x4f projection = Math::orthographicProjection(
        {0.0f, 0.0f},
        {static_cast<float>(extent.width), static_cast<float>(extent.height)},
        nearPlane,
        farPlane);

    return projection * view;
}
} // namespace VOG::Engine
