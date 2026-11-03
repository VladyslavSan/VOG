#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Math/Math.hpp>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>
#include <glm/vector_relational.hpp>

namespace VOG::Math
{
/** Where a camera is and which way it looks; direction and up must be normalized. */
struct CameraPose
{
    Vector3f position;
    Vector3f direction;
    Vector3f up;
};

Matrix4x4f calculateCameraMatrix(const CameraPose& pose);

Matrix4x4f orthographicProjection(const Vector2f& position,
                                  const Vector2f& viewportSize,
                                  float           zNear,
                                  float           zFar);
} // namespace VOG::Math
