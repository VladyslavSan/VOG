#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Scene/Scene.hpp>
#include <VOG/Math/Math.hpp>

#include <cstdint>
#include <string_view>

namespace Rally
{
/** The size of the court in world units: the camera shows this height, so a unit is a pixel at
 *  the height of the window. */
inline constexpr float gCourtHalfWidth  = 640.0f;
inline constexpr float gCourtHalfHeight = 360.0f;
inline constexpr float gCourtHeight     = 2.0f * gCourtHalfHeight;

inline constexpr float gPaddleHalfWidth       = 14.0f;
inline constexpr float gPaddleStartHalfHeight = 90.0f;
inline constexpr float gPaddleX               = 590.0f;
inline constexpr float gBallHalfSize          = 14.0f;

/** The bumper stands in the middle of the court and drifts up and down it; the ball rebounds off
 *  it. It is tall enough to be in the way, and its drift (a cosine, from the top) stays inside
 *  the court. */
inline constexpr float gBumperHalfWidth     = 24.0f;
inline constexpr float gBumperHalfHeight    = 64.0f;
inline constexpr float gBumperAmplitude     = 270.0f;
inline constexpr float gBumperPeriodSeconds = 5.0f;

inline constexpr int gPointsToWin = 7;

/** A side of the court. */
enum class Side : std::uint8_t
{
    eLeft,
    eRight
};

/** The component of a paddle that its behaviour and the ball share: how tall half of it is. The
 *  twist of the game is that every hit makes it smaller. */
struct PaddleState
{
    float halfHeight = gPaddleStartHalfHeight;
};

/** The component of the bumper: how long it has drifted, and whether it drifts. It waits at the
 *  top of the court, out of the way of a ball that is served, until a ball flies. */
struct BumperState
{
    float age      = 0.0f;
    bool  drifting = false;
};

/** The component of the referee: the points of the match. */
struct Score
{
    int  left      = 0;
    int  right     = 0;
    bool matchOver = false;
};

/** The two keys that move a paddle. */
struct PaddleKeys
{
    VOG::Engine::Key upKey;
    VOG::Engine::Key downKey;
};

/** Moves its paddle with two keys, within the court, and gives it the height it has now. */
class Paddle final : public VOG::Engine::Behaviour
{
public:
    explicit Paddle(PaddleKeys keys);

    void update(const VOG::Engine::FrameContext& frame) override;

private:
    PaddleKeys mKeys;
};

/** Drifts its object up and down the middle of the court while its BumperState says so. */
class Bumper final : public VOG::Engine::Behaviour
{
public:
    void update(const VOG::Engine::FrameContext& frame) override;
};

/** The object of the scene called @p name.
 *  @throws std::logic_error if there is none. */
VOG::Engine::GameObject findOrThrow(VOG::Engine::Scene& scene, std::string_view name);

/**
 * The ball. It waits for the space key to be served, flies, bounces off the walls, the bumper and
 * the paddles, and leaves a trail of ghosts. A hit speeds it up and shrinks the paddle that hit it;
 * a miss scores for the other side, makes the paddles tall again, and serves the next ball to the
 * side that missed. The match goes to seven points; after it, the space key starts the next one.
 */
class Ball final : public VOG::Engine::Behaviour
{
public:
    Ball(VOG::Engine::Mesh ghostMesh, VOG::Engine::Material ghostMaterial);

    void start() override;
    void update(const VOG::Engine::FrameContext& frame) override;

private:
    enum class Phase : std::uint8_t
    {
        eWaiting,
        eFlying
    };

    void serve();
    void step(float seconds);
    bool hits(VOG::Engine::GameObject paddle);
    void bounceOff(VOG::Engine::GameObject paddle, float direction);
    void reboundOffBumper();
    void score(Side scorer);
    void leaveGhost();

    VOG::Engine::Mesh     mGhostMesh;
    VOG::Engine::Material mGhostMaterial;

    VOG::Engine::GameObject mLeft;
    VOG::Engine::GameObject mRight;
    VOG::Engine::GameObject mBumper;
    VOG::Engine::GameObject mReferee;

    Phase               mPhase = Phase::eWaiting;
    VOG::Math::Vector2f mVelocity{0.0f};
    float               mSpeed       = 0.0f;
    float               mServeToward = 1.0f;
    float               mGhostTimer  = 0.0f;
    bool                mSlantUp     = false;
};

/** Shrinks its object to nothing in the time it is given, then destroys it. */
class Ghost final : public VOG::Engine::Behaviour
{
public:
    explicit Ghost(float seconds);

    void start() override;
    void update(const VOG::Engine::FrameContext& frame) override;

private:
    float               mSeconds;
    float               mAge = 0.0f;
    VOG::Math::Vector3f mStartScale{1.0f};
};

/** One of the lit or dark marks of the score: lit when its side has scored as many points as
 *  the number of the mark. */
class Pip final : public VOG::Engine::Behaviour
{
public:
    Pip(Side side, int number, VOG::Engine::Material lit, VOG::Engine::Material dark);

    void start() override;
    void update(const VOG::Engine::FrameContext& frame) override;

private:
    Side                    mSide;
    int                     mNumber;
    VOG::Engine::Material   mLit;
    VOG::Engine::Material   mDark;
    VOG::Engine::GameObject mReferee;
};

/** Holds the score, quits on the escape key and, if it is given a number of frames, after them. */
class Referee final : public VOG::Engine::Behaviour
{
public:
    /** @p maxFrames of 0 means no limit. */
    explicit Referee(int maxFrames);

    void update(const VOG::Engine::FrameContext& frame) override;

private:
    int mMaxFrames;
    int mFrames = 0;
};
} // namespace Rally
