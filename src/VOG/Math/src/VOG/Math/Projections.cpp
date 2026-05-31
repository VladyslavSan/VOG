#include "VOG/Math/Projections.hpp"

namespace VOG::Math
{
Matrix4x4f
calculateCameraMatrix(const Vector3f& position, const Vector3f& direction, const Vector3f& upVec)
{
    constexpr auto isNormalized = [](const Vector3f& vector)
    {
        const auto normalized = glm::normalize(vector);

        return glm::all(glm::epsilonEqual(vector, normalized, glm::epsilon<float>()));
    };

    VOG_ASSERT(isNormalized(direction));
    VOG_ASSERT(isNormalized(upVec));

    // Uninitialized constructor, we fill all the values afterward.
    Matrix4x4f result;

    result[0] = {glm::cross(upVec, direction), 0.0f};
    result[1] = {upVec, 0.0f};
    result[2] = {direction, 0.0f};
    result[3] = {position, 1.0f};

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
