#include "VOG/Math/Projections.hpp"

#include <cmath>

namespace VOG::Math
{
Matrix4x4f
calculateCameraMatrix(const CameraPose& pose)
{
    // A length check: a unit quaternion rotates +Z to a vector a few ULP off, per component.
    constexpr float kLengthTolerance = 1e-5f;
    constexpr auto  isNormalized     = [](const Vector3f& vector)
    {
        return std::abs(glm::length(vector) - 1.0f) <= kLengthTolerance;
    };

    VOG_ASSERT(isNormalized(pose.direction));
    VOG_ASSERT(isNormalized(pose.up));

    // Uninitialized constructor, we fill all the values afterward.
    Matrix4x4f result;

    result[0] = {glm::cross(pose.up, pose.direction), 0.0f};
    result[1] = {pose.up, 0.0f};
    result[2] = {pose.direction, 0.0f};
    result[3] = {pose.position, 1.0f};

    return result;
}

Matrix4x4f
orthographicProjection(const Vector2f& position,
                       const Vector2f& viewportSize,
                       const float     zNear,
                       const float     zFar)
{
    constexpr float kTwo = 2.0f;
    Matrix4x4f result = {Vector4f{kTwo / viewportSize.x, 0.0f, 0.0f, 0.0f},
                         Vector4f{0.0f, kTwo / viewportSize.y, 0.0f, 0.0f},
                         Vector4f{0.0f, 0.0f, 1.0 / (zFar - zNear), 0.0f},
                         Vector4f{-kTwo * position / viewportSize, -zNear / (zFar - zNear), 1.0f}};

    return result;
}
} // namespace VOG::Math
