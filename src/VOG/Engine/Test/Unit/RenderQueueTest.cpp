#include <gtest/gtest.h>

#include "RenderQueueTestAccess.hpp"

using namespace VOG::Engine;

namespace VOG::Tests
{
// NOLINTBEGIN(readability-magic-numbers): the numbers only tell cameras apart.
TEST(RenderQueueTest, lastSetCameraWins)
{
    RenderQueue queue;

    queue.setCamera(CameraView{.nearPlane = 2.0f});
    queue.setCamera(CameraView{.nearPlane = 3.0f});

    EXPECT_EQ(RenderQueueTestAccess::camera(queue).nearPlane, 3.0f);
}

TEST(RenderQueueTest, defaultCameraIsDefaultView)
{
    const RenderQueue queue;
    const CameraView  defaultView;

    const CameraView& camera = RenderQueueTestAccess::camera(queue);
    EXPECT_EQ(camera.position, defaultView.position);
    EXPECT_EQ(camera.direction, defaultView.direction);
    EXPECT_EQ(camera.up, defaultView.up);
    EXPECT_EQ(camera.nearPlane, defaultView.nearPlane);
    EXPECT_EQ(camera.farPlane, defaultView.farPlane);
    EXPECT_TRUE(RenderQueueTestAccess::draws(queue).empty());
}

// NOLINTEND(readability-magic-numbers)
} // namespace VOG::Tests
