#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "Behaviours.hpp"
#include "Game.hpp"

using namespace Rally;
namespace E = VOG::Engine;

namespace VOG::Tests
{
namespace
{
constexpr float gFrameSeconds = 1.0f / 60.0f;

/** The game, run without a window or a renderer: every handle it draws with is empty, and the
 *  input is written by the test. */
class RallyTest : public testing::Test
{
protected:
    RallyTest()
    {
        buildGame(mScene, {}, 0);
        mBall    = findOrThrow(mScene, "ball");
        mLeft    = findOrThrow(mScene, "left paddle");
        mRight   = findOrThrow(mScene, "right paddle");
        mBumper  = findOrThrow(mScene, "bumper");
        mReferee = findOrThrow(mScene, "referee");
    }

    /** One frame; what was pressed since the frame before is seen by it. */
    void
    tick()
    {
        mScene.update({.deltaSeconds = gFrameSeconds, .input = mInput});
        E::Input::Writer{mInput}.beginFrame();
    }

    void
    ticks(int frames)
    {
        for (int frame = 0; frame < frames; ++frame)
        {
            tick();
        }
    }

    void
    press(E::Key key)
    {
        E::Input::Writer{mInput}.press(key);
    }

    void
    release(E::Key key)
    {
        E::Input::Writer{mInput}.release(key);
    }

    /** Serves: the space key, in a frame of its own. */
    void
    serve()
    {
        press(E::Key::eSpace);
        tick();
        release(E::Key::eSpace);
    }

    float
    bumperY()
    {
        return mBumper.getTransform().position.y;
    }

    BumperState&
    bumper()
    {
        return mBumper.getComponent<BumperState>();
    }

    VOG::Math::Vector3f&
    ballPosition()
    {
        return mBall.getTransform().position;
    }

    Score&
    score()
    {
        return mReferee.getComponent<Score>();
    }

    /** How many ghosts the scene has. */
    int
    ghosts()
    {
        int count = 0;
        mScene.each<E::Name>(
            [&](E::GameObject /*object*/, E::Name& name)
            {
                count += name.value == "ghost" ? 1 : 0;
            });

        return count;
    }

    E::Scene      mScene;
    E::Input      mInput;
    E::GameObject mBall;
    E::GameObject mLeft;
    E::GameObject mRight;
    E::GameObject mBumper;
    E::GameObject mReferee;
};
} // namespace

// The numbers are positions and speeds of the court; the score of a test comes from the gtest
// assertion macros.
// NOLINTBEGIN(readability-magic-numbers, readability-function-cognitive-complexity)

/** The ball stays at the middle until the space key serves it, then flies toward the right side,
 *  where the first ball goes. */
TEST_F(RallyTest, theBallWaitsForTheSpaceKeyAndThenFlies)
{
    ticks(30);
    EXPECT_EQ(ballPosition().x, 0.0f);
    EXPECT_EQ(ballPosition().y, 0.0f);

    serve();
    ticks(5);
    EXPECT_GT(ballPosition().x, 0.0f);
}

/** A hit turns the ball around, makes it 8 percent faster and the paddle that hit it 7 percent
 *  shorter. */
TEST_F(RallyTest, aHitSpeedsTheBallUpAndShrinksThePaddle)
{
    serve();
    ticks(2);
    const auto speedOf = [&]
    {
        const VOG::Math::Vector3f before = ballPosition();
        tick();
        const VOG::Math::Vector3f after = ballPosition();

        return std::hypot(after.x - before.x, after.y - before.y) / gFrameSeconds;
    };
    const float slow = speedOf();

    ballPosition().x = gPaddleX - 20.0f;
    ballPosition().y = 0.0f;
    tick();
    // Out of the paddle, and on its way back.
    EXPECT_LT(ballPosition().x, gPaddleX - 20.0f);
    const float outside = ballPosition().x;
    tick();
    EXPECT_LT(ballPosition().x, outside);

    EXPECT_NEAR(
        mRight.getComponent<PaddleState>().halfHeight, gPaddleStartHalfHeight * 0.93f, 0.01f);
    EXPECT_EQ(mLeft.getComponent<PaddleState>().halfHeight, gPaddleStartHalfHeight);
    EXPECT_NEAR(speedOf(), slow * 1.08f, slow * 0.01f);
    // The paddle is as tall as the state says, the next frame.
    EXPECT_NEAR(mRight.getTransform().scale.y, gPaddleStartHalfHeight * 0.93f, 0.01f);
}

/** A miss scores for the other side, makes the paddles tall again, and the next ball goes to the
 *  side that missed. */
TEST_F(RallyTest, aMissScoresRegrowsThePaddlesAndServesTowardTheLoser)
{
    mRight.getComponent<PaddleState>().halfHeight = 40.0f;
    mLeft.getComponent<PaddleState>().halfHeight  = 50.0f;

    serve();
    ballPosition().y = 300.0f;
    ballPosition().x = 600.0f;
    // The right paddle is at the middle: this ball passes above it.
    ticks(20);

    EXPECT_EQ(score().left, 1);
    EXPECT_EQ(score().right, 0);
    EXPECT_EQ(mRight.getComponent<PaddleState>().halfHeight, gPaddleStartHalfHeight);
    EXPECT_EQ(mLeft.getComponent<PaddleState>().halfHeight, gPaddleStartHalfHeight);
    EXPECT_EQ(ballPosition().x, 0.0f);

    // Waiting for the space key; then toward the side that missed, the right one.
    ticks(10);
    EXPECT_EQ(ballPosition().x, 0.0f);
    serve();
    ticks(5);
    EXPECT_GT(ballPosition().x, 0.0f);
}

/** Seven points end the match; the next space key starts another one from nothing. */
TEST_F(RallyTest, sevenPointsEndTheMatchAndTheSpaceKeyStartsTheNext)
{
    for (int point = 0; point < gPointsToWin; ++point)
    {
        EXPECT_FALSE(score().matchOver);
        serve();
        ballPosition().x = 700.0f;
        ballPosition().y = 300.0f;
        tick();
    }
    EXPECT_EQ(score().left + score().right, gPointsToWin);
    EXPECT_TRUE(score().matchOver);

    serve();
    EXPECT_FALSE(score().matchOver);
    EXPECT_EQ(score().left + score().right, 0);
}

/** The bumper waits at the top of the court until a ball flies, then drifts down and up the
 *  middle; a point puts it back at the top, where it waits. */
TEST_F(RallyTest, theBumperDriftsWhileABallFliesAndWaitsAtTheTopOtherwise)
{
    ticks(60);
    EXPECT_EQ(bumperY(), gBumperAmplitude);
    EXPECT_EQ(mBumper.getTransform().position.x, 0.0f);

    serve();
    ticks(30);
    EXPECT_LT(bumperY(), gBumperAmplitude - 20.0f);
    EXPECT_GT(bumperY(), -gBumperAmplitude);
    EXPECT_EQ(mBumper.getTransform().position.x, 0.0f);

    // A miss: the ball is past the right paddle.
    ballPosition().x = 700.0f;
    ticks(2);
    EXPECT_EQ(score().left, 1);
    EXPECT_EQ(bumperY(), gBumperAmplitude);
    ticks(30);
    EXPECT_EQ(bumperY(), gBumperAmplitude);
}

/** A ball that flies into the side of the bumper comes back: it does not pass through. The bumper
 *  is put in the middle of its drift, where it is level with the ball. */
TEST_F(RallyTest, theBallReboundsOffTheSideOfTheBumper)
{
    serve();
    bumper().age     = gBumperPeriodSeconds / 4.0f;
    ballPosition().x = -60.0f;
    ballPosition().y = 0.0f;

    // Flying right at 420 per second, it reaches the bumper (at -38) in about 3 frames.
    ticks(8);
    const float edge = -(gBumperHalfWidth + gBallHalfSize);
    EXPECT_LE(ballPosition().x, edge + 1.0f);
    const float after = ballPosition().x;
    ticks(2);
    EXPECT_LT(ballPosition().x, after);
}

/** A ball that comes up from below into the bumper is turned back down, and does not pass the
 *  bumper, which is coming down on it. */
TEST_F(RallyTest, theBallReboundsOffTheBottomOfTheBumper)
{
    serve();
    bumper().age     = gBumperPeriodSeconds / 4.0f;
    ballPosition().x = 0.0f;
    ballPosition().y = -(gBumperHalfHeight + gBallHalfSize) + 8.0f;

    ticks(3);
    EXPECT_LT(ballPosition().y, bumperY() - gBumperHalfHeight);
    EXPECT_LT(mBumper.getTransform().position.y, 0.0f);
}

/** A paddle moves with its two keys and stops at the edge of the court, and the other one does
 *  not follow. */
TEST_F(RallyTest, paddlesFollowTheirKeysAndStopAtTheEdges)
{
    press(E::Key::eW);
    ticks(300);
    EXPECT_NEAR(mLeft.getTransform().position.y, gCourtHalfHeight - gPaddleStartHalfHeight, 0.01f);
    EXPECT_EQ(mRight.getTransform().position.y, 0.0f);

    release(E::Key::eW);
    press(E::Key::eS);
    ticks(600);
    EXPECT_NEAR(
        mLeft.getTransform().position.y, -(gCourtHalfHeight - gPaddleStartHalfHeight), 0.01f);

    release(E::Key::eS);
    press(E::Key::eUp);
    ticks(10);
    EXPECT_GT(mRight.getTransform().position.y, 0.0f);
}

/** The ball leaves a trail of ghosts while it flies, and they are gone soon after it stops. */
TEST_F(RallyTest, theBallLeavesGhostsThatVanish)
{
    EXPECT_EQ(ghosts(), 0);

    serve();
    ticks(20);
    EXPECT_GT(ghosts(), 0);
    EXPECT_LT(ghosts(), 12);

    ballPosition().x = 700.0f;
    ticks(40);
    EXPECT_EQ(ghosts(), 0);
}

/** The referee quits on the escape key, and after the number of frames it is given. */
TEST_F(RallyTest, theRefereeQuitsOnEscape)
{
    EXPECT_FALSE(mScene.quitRequested());

    press(E::Key::eEscape);
    tick();
    EXPECT_TRUE(mScene.quitRequested());
}

TEST(RallyFramesTest, theRefereeQuitsAfterTheFramesItIsGiven)
{
    E::Scene scene;
    buildGame(scene, {}, 3);

    scene.update({});
    scene.update({});
    EXPECT_FALSE(scene.quitRequested());
    scene.update({});
    EXPECT_TRUE(scene.quitRequested());
}

// NOLINTEND(readability-magic-numbers, readability-function-cognitive-complexity)
} // namespace VOG::Tests
