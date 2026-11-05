#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Math/Math.hpp>

namespace VOG::Engine
{
/** The view of one frame, as the renderer draws it: orthographic, one world unit per pixel,
 * centered on the view axis. The camera's up points up the screen: with the default direction and
 * up, world +Y is up and +X is right. The default is the view of the Camera component at the
 * identity transform. */
struct CameraView
{
    Math::Vector3f position{0.0f};
    Math::Vector3f direction{0.0f, 0.0f, 1.0f};
    Math::Vector3f up{0.0f, 1.0f, 0.0f};
    float          nearPlane = 1.0f;
    float          farPlane  = 200.0f; // NOLINT(readability-magic-numbers)

    /** World space to view space; direction and up must be normalized. */
    Math::Matrix4x4f view() const;

    /** View space to clip space for a color target of @p extent. */
    Math::Matrix4x4f projection(Extent extent) const;

    /** World space to clip space: projection(extent) * view(). */
    Math::Matrix4x4f viewProjection(Extent extent) const;
};
} // namespace VOG::Engine
