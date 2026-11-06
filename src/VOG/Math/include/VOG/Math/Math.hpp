#pragma once

#include <VOG/Math/GLMConfig.hpp>

#include <glm/gtc/quaternion.hpp>

namespace VOG::Math
{
using Vector2i = glm::vec<2, int>;
using Vector3i = glm::vec<3, int>;
using Vector4i = glm::vec<4, int>;

using Vector2u = glm::vec<2, unsigned int>;
using Vector3u = glm::vec<3, unsigned int>;
using Vector4u = glm::vec<4, unsigned int>;

using Vector2f = glm::vec<2, float>;
using Vector3f = glm::vec<3, float>;
using Vector4f = glm::vec<4, float>;

using Vector2d = glm::vec<2, double>;
using Vector3d = glm::vec<3, double>;
using Vector4d = glm::vec<4, double>;

using Quaternionf = glm::qua<float>;

using Matrix3x3f = glm::mat<3, 3, float>;
using Matrix4x4f = glm::mat<4, 4, float>;
using Matrix4x4d = glm::mat<4, 4, double>;
} // namespace VOG::Math
