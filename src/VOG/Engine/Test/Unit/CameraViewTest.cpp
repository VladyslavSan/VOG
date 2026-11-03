#include <VOG/Engine/Renderer/CameraView.hpp>

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>

#include <cmath>

using namespace VOG::Engine;
using VOG::Math::Vector4f;

namespace VOG::Tests
{
/**
 * One world unit is one pixel: the default camera maps the edge of the target to clip x = 1. World
 * +Y is up the screen, and Vulkan clip y = -1 is the top of the target, so +Y maps to negative y.
 */
TEST(CameraViewTest, defaultCameraMapsPixelsToClipSpace)
{
    constexpr float  kTolerance = 1e-6f;
    const CameraView camera;

    const VOG::Math::Matrix4x4f square = camera.viewProjection({.width = 64u, .height = 64u});

    const Vector4f edge = square * Vector4f{32.0f, 0.0f, 20.0f, 1.0f};
    EXPECT_NEAR(edge.x, 1.0f, kTolerance);
    EXPECT_NEAR(edge.y, 0.0f, kTolerance);
    EXPECT_NEAR(edge.w, 1.0f, kTolerance);

    // Twice the target, half the clip coordinate.
    const Vector4f wider =
        camera.viewProjection({.width = 128u, .height = 64u}) * Vector4f{32.0f, 0.0f, 20.0f, 1.0f};
    EXPECT_NEAR(wider.x, 0.5f, kTolerance);

    const Vector4f above = square * Vector4f{0.0f, 16.0f, 20.0f, 1.0f};
    EXPECT_NEAR(above.y, -0.5f, kTolerance);
    const Vector4f below = square * Vector4f{0.0f, -16.0f, 20.0f, 1.0f};
    EXPECT_NEAR(below.y, 0.5f, kTolerance);
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
