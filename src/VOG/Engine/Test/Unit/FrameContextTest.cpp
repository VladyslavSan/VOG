#include <VOG/Engine/Renderer/FrameContext.hpp>

#include <gtest/gtest.h>

using namespace VOG::Engine;

namespace VOG::Tests
{
TEST(FrameContextTest, frameDeltaClamps)
{
    EXPECT_EQ(frameDelta(0.01), 0.01f);
    EXPECT_EQ(frameDelta(10.0), gMaxDeltaSeconds);
    EXPECT_EQ(frameDelta(-1.0), 0.0f);
}
} // namespace VOG::Tests
