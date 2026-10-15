#include <VOG/Engine/Renderer/Camera.hpp>

#include <gtest/gtest.h>

using namespace VOG::Engine;
using VOG::Math::Vector4f;

namespace VOG::Tests
{
/** One world unit is one pixel: the default camera maps the edge of the target to clip x = 1. */
TEST(CameraTest, defaultCameraMapsPixelsToClipSpace)
{
    constexpr float kTolerance = 1e-6f;
    const Camera    camera;

    const Vector4f edge =
        camera.viewProjection({.width = 64u, .height = 64u}) * Vector4f{32.0f, 0.0f, 20.0f, 1.0f};
    EXPECT_NEAR(edge.x, 1.0f, kTolerance);
    EXPECT_NEAR(edge.y, 0.0f, kTolerance);
    EXPECT_NEAR(edge.w, 1.0f, kTolerance);

    // Twice the target, half the clip coordinate.
    const Vector4f wider =
        camera.viewProjection({.width = 128u, .height = 64u}) * Vector4f{32.0f, 0.0f, 20.0f, 1.0f};
    EXPECT_NEAR(wider.x, 0.5f, kTolerance);
}
} // namespace VOG::Tests
