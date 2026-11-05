#include <VOG/Engine/Renderer/CameraView.hpp>

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace VOG::Engine;
using VOG::Math::Vector4f;

namespace VOG::Tests
{
/**
 * The default camera shows 10 world units from the bottom of the target to the top, and as many
 * sideways as the aspect ratio makes of it: on a square target the edge is 5 units out, on a target
 * twice as wide it is 10. World +Y is up the screen, and Vulkan clip y = -1 is the top of the
 * target, so +Y maps to negative y. Depth is 0 at the near plane, -1000 behind the camera, and 1 at
 * the far plane, so the plane of the camera, z = 0, is half way.
 */
TEST(CameraViewTest, defaultCameraMapsWorldUnitsToClipSpace)
{
    constexpr float  kTolerance = 1e-6f;
    const CameraView camera;

    const VOG::Math::Matrix4x4f square = camera.viewProjection({.width = 64u, .height = 64u});

    const Vector4f edge = square * Vector4f{5.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(edge.x, 1.0f, kTolerance);
    EXPECT_NEAR(edge.y, 0.0f, kTolerance);
    EXPECT_NEAR(edge.z, 0.5f, kTolerance);
    EXPECT_NEAR(edge.w, 1.0f, kTolerance);

    // Twice the width, the same height: the unit keeps its height and the edge moves out.
    const VOG::Math::Matrix4x4f wide = camera.viewProjection({.width = 128u, .height = 64u});
    EXPECT_NEAR((wide * Vector4f{5.0f, 0.0f, 0.0f, 1.0f}).x, 0.5f, kTolerance);
    EXPECT_NEAR((wide * Vector4f{10.0f, 0.0f, 0.0f, 1.0f}).x, 1.0f, kTolerance);
    EXPECT_NEAR((wide * Vector4f{0.0f, 5.0f, 0.0f, 1.0f}).y, -1.0f, kTolerance);

    const Vector4f above = square * Vector4f{0.0f, 2.5f, 0.0f, 1.0f};
    EXPECT_NEAR(above.y, -0.5f, kTolerance);
    const Vector4f below = square * Vector4f{0.0f, -2.5f, 0.0f, 1.0f};
    EXPECT_NEAR(below.y, 0.5f, kTolerance);

    EXPECT_NEAR((square * Vector4f{0.0f, 0.0f, -1000.0f, 1.0f}).z, 0.0f, kTolerance);
    EXPECT_NEAR((square * Vector4f{0.0f, 0.0f, 1000.0f, 1.0f}).z, 1.0f, kTolerance);
}

/**
 * A perspective camera shows, at the distance d, the height 2 d tan(fov / 2), whatever the aspect:
 * a point d tan(fov / 2) above the axis at depth d is at the top of the target, after the divide.
 * A nearer point has a smaller depth, which is what a depth test keeps.
 */
TEST(CameraViewTest, perspectiveCameraShowsTheFieldOfViewAtADistance)
{
    constexpr float kTolerance = 1e-5f;
    constexpr float kFov       = 1.0f;
    constexpr float kDistance  = 20.0f;

    CameraView camera;
    camera.projection =
        Perspective{.verticalFovRadians = kFov, .nearPlane = 1.0f, .farPlane = 100.0f};

    const VOG::Math::Matrix4x4f matrix = camera.viewProjection({.width = 128u, .height = 64u});

    const float    reach = kDistance * std::tan(kFov / 2.0f);
    const Vector4f top   = matrix * Vector4f{0.0f, reach, kDistance, 1.0f};
    EXPECT_NEAR(top.y / top.w, -1.0f, kTolerance);

    // The width is the height times the aspect.
    const Vector4f side = matrix * Vector4f{2.0f * reach, 0.0f, kDistance, 1.0f};
    EXPECT_NEAR(side.x / side.w, 1.0f, kTolerance);

    const Vector4f nearer  = matrix * Vector4f{0.0f, 0.0f, 10.0f, 1.0f};
    const Vector4f farther = matrix * Vector4f{0.0f, 0.0f, 50.0f, 1.0f};
    EXPECT_LT(nearer.z / nearer.w, farther.z / farther.w);
    EXPECT_GT(nearer.z / nearer.w, 0.0f);
    EXPECT_LT(farther.z / farther.w, 1.0f);
}

/**
 * Scene::onDraw hands the view the rotated +Z and +Y of a unit quaternion, which are a few ULP off
 * per component. In a Debug build a per-component epsilon check aborts on 117 of 360 whole yaws.
 */
TEST(CameraViewTest, rotatedCameraDoesNotAbortOnUnitVectorRounding)
{
    constexpr int kDegreesInCircle = 360;
    for (int yaw = 0; yaw < kDegreesInCircle; ++yaw)
    {
        const VOG::Math::Quaternionf rotation = glm::angleAxis(
            glm::radians(static_cast<float>(yaw)), VOG::Math::Vector3f{0.0f, 1.0f, 0.0f});
        CameraView camera;
        camera.direction = rotation * VOG::Math::Vector3f{0.0f, 0.0f, 1.0f};
        camera.up        = rotation * VOG::Math::Vector3f{0.0f, 1.0f, 0.0f};

        const Vector4f clip = camera.viewProjection({.width = 64u, .height = 64u}) *
                              Vector4f{0.0f, 0.0f, 20.0f, 1.0f};
        EXPECT_TRUE(std::isfinite(clip.x)) << "yaw " << yaw;
    }
}
} // namespace VOG::Tests
