#pragma once

#include <VOG/Engine/Renderer/Extent.hpp>
#include <VOG/Math/Math.hpp>

#include <numbers>
#include <variant>

namespace VOG::Engine
{
/** The distance of the far plane of a projection by default, and the near plane of an orthographic
 *  one behind the camera. */
inline constexpr float gDefaultDepthRange = 1000.0f;

/**
 * Parallel projection in world units: the view shows @p height units from the bottom of the target
 * to the top, and as many sideways as the aspect ratio of the target makes of it, so a resize of
 * the window changes the pixels of a unit and not what is seen. The default near plane is behind
 * the camera, so an object at the position of the camera, such as the default Transform under the
 * default Camera, is drawn.
 */
struct Orthographic
{
    float height    = 10.0f; // NOLINT(readability-magic-numbers)
    float nearPlane = -gDefaultDepthRange;
    float farPlane  = gDefaultDepthRange;

    bool operator==(const Orthographic&) const = default;
};

/** Perspective projection; the field of view is the vertical one. */
struct Perspective
{
    float verticalFovRadians =
        std::numbers::pi_v<float> / 3.0f; // NOLINT(readability-magic-numbers)
    float nearPlane = 0.1f;               // NOLINT(readability-magic-numbers)
    float farPlane  = gDefaultDepthRange;

    bool operator==(const Perspective&) const = default;
};

using Projection = std::variant<Orthographic, Perspective>;

/**
 * The view of one frame, as the renderer draws it. The camera looks along +Z with +Y up. World +Y
 * is up the screen: at the default pose +X is right, +Y is up and +Z goes into the screen (the
 * world is left-handed). The camera's up points up the screen. The default is the view of the
 * Camera component at the identity transform.
 */
struct CameraView
{
    Math::Vector3f position{0.0f};
    Math::Vector3f direction{0.0f, 0.0f, 1.0f};
    Math::Vector3f up{0.0f, 1.0f, 0.0f};
    Projection     projection = Orthographic{};

    /** What the color target is cleared to before the frame draws. */
    Math::Vector4f clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    /** World space to view space; direction and up must be normalized. */
    Math::Matrix4x4f view() const;

    /** View space to clip space for a color target of @p extent, which has no zero side. Depth is
     *  0 at the near plane and 1 at the far plane. */
    Math::Matrix4x4f projectionMatrix(Extent extent) const;

    /** World space to clip space: projectionMatrix(extent) * view(). */
    Math::Matrix4x4f viewProjection(Extent extent) const;
};
} // namespace VOG::Engine
