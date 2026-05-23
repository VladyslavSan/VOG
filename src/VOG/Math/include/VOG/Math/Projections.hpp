#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Math/Math.hpp>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/vector_relational.hpp>

namespace VOG::Math
{
Matrix4x4f
calculateCameraMatrix(const Vector3f& position, const Vector3f& direction, const Vector3f& up);

Matrix4x4f orthographicProjection(const Vector2f& position,
                                  const Vector2f& viewportSize,
                                  const float     zNear,
                                  const float     zFar);
} // namespace VOG::Math
