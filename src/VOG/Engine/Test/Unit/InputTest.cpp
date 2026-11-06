#include <VOG/Engine/Renderer/Input.hpp>

#include <gtest/gtest.h>

using namespace VOG::Engine;

namespace VOG::Tests
{
// NOLINTBEGIN(readability-magic-numbers): the numbers are positions of a mouse.
/**
 * A key goes through its frames: pressed in the frame that saw it go down and no other; down for as
 * long as it is held; released in the frame that saw it go up and no other.
 */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(InputTest, aKeyIsPressedOnceStaysDownAndIsReleasedOnce)
{
    Input         input;
    Input::Writer writer{input};

    EXPECT_FALSE(input.isDown(Key::eW));

    writer.beginFrame();
    writer.press(Key::eW);
    EXPECT_TRUE(input.wasPressed(Key::eW));
    EXPECT_TRUE(input.isDown(Key::eW));
    EXPECT_FALSE(input.wasReleased(Key::eW));
    EXPECT_FALSE(input.isDown(Key::eS));

    // The next frame: held.
    writer.beginFrame();
    EXPECT_FALSE(input.wasPressed(Key::eW));
    EXPECT_TRUE(input.isDown(Key::eW));

    // A press of a key that is down is no new press, in this frame or the next.
    writer.press(Key::eW);
    EXPECT_FALSE(input.wasPressed(Key::eW));

    writer.beginFrame();
    writer.release(Key::eW);
    EXPECT_FALSE(input.isDown(Key::eW));
    EXPECT_TRUE(input.wasReleased(Key::eW));
    EXPECT_FALSE(input.wasPressed(Key::eW));

    writer.beginFrame();
    EXPECT_FALSE(input.isDown(Key::eW));
    EXPECT_FALSE(input.wasReleased(Key::eW));

    // A release of a key that is up is no release.
    writer.release(Key::eW);
    EXPECT_FALSE(input.wasReleased(Key::eW));
}

/** The same for a mouse button, and the position is what was written; the keys are not touched. */
// The score comes from the gtest assertion macros.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(InputTest, aMouseButtonGoesThroughTheSameFramesAndThePositionIsWhatWasWritten)
{
    Input         input;
    Input::Writer writer{input};

    writer.beginFrame();
    writer.press(MouseButton::eRight);
    writer.moveMouse({12.5f, 40.0f});
    EXPECT_TRUE(input.wasPressed(MouseButton::eRight));
    EXPECT_TRUE(input.isDown(MouseButton::eRight));
    EXPECT_FALSE(input.isDown(MouseButton::eLeft));
    EXPECT_FALSE(input.isDown(Key::eA));
    EXPECT_EQ(input.mousePosition(), (VOG::Math::Vector2f{12.5f, 40.0f}));

    writer.beginFrame();
    EXPECT_FALSE(input.wasPressed(MouseButton::eRight));
    EXPECT_TRUE(input.isDown(MouseButton::eRight));
    // The position stays until the mouse moves.
    EXPECT_EQ(input.mousePosition(), (VOG::Math::Vector2f{12.5f, 40.0f}));

    writer.release(MouseButton::eRight);
    EXPECT_TRUE(input.wasReleased(MouseButton::eRight));
    EXPECT_FALSE(input.isDown(MouseButton::eRight));

    writer.beginFrame();
    EXPECT_FALSE(input.wasReleased(MouseButton::eRight));
}

/** Every key has a bit of its own, also the last one. */
TEST(InputTest, everyKeyHasABitOfItsOwn)
{
    Input         input;
    Input::Writer writer{input};

    writer.press(Key::eLeftShift);

    EXPECT_TRUE(input.isDown(Key::eLeftShift));
    for (std::size_t key = 0u; key + 1u < gKeyCount; ++key)
    {
        EXPECT_FALSE(input.isDown(static_cast<Key>(key))) << key;
    }
}

// NOLINTEND(readability-magic-numbers)
} // namespace VOG::Tests
