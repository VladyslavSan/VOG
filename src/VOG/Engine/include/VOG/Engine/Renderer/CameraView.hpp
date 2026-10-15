#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Math/Math.hpp>

namespace VOG::Engine
{
/** The view of one frame, as the renderer draws it: orthographic, one world unit per pixel,
 * centered on the view axis. The default is the view of the Camera component at the identity
 * transform. */
struct CameraView
{
    Math::Vector3f position{0.0f};
    Math::Vector3f direction{0.0f, 0.0f, 1.0f};
    Math::Vector3f up{0.0f, 1.0f, 0.0f};
    float          nearPlane = 1.0f;
    float          farPlane  = 200.0f; // NOLINT(readability-magic-numbers)

    /** World space to clip space for a color target of @p extent; direction and up must be
     *  normalized. */
    Math::Matrix4x4f viewProjection(Extent extent) const;
};
} // namespace VOG::Engine
