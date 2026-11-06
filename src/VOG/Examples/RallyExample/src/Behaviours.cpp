#include "Behaviours.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace Rally
{
namespace
{
using VOG::Engine::GameObject;
using VOG::Engine::Key;
using VOG::Engine::MeshRenderer;
using VOG::Engine::Transform;

constexpr float gPaddleSpeed   = 520.0f;
constexpr float gMinHalfHeight = 34.0f;
constexpr float gShrinkPerHit  = 0.93f;
constexpr float gSpeedUpPerHit = 1.08f;
constexpr float gStartSpeed    = 420.0f;
constexpr float gMaxSpeed      = 1500.0f;
constexpr float gSteepness     = 0.75f;
constexpr float gMaxStepLength = 8.0f;
constexpr float gGhostEvery    = 0.035f;
constexpr float gGhostSeconds  = 0.3f;
constexpr float gGhostSize     = 0.8f;
constexpr float gGhostDepth    = 0.5f;
constexpr float gScoreMargin   = 40.0f;
constexpr float gServeSlant    = 0.35f;
constexpr float gTwoPi         = 2.0f * std::numbers::pi_v<float>;

float
sign(float value)
{
    return value < 0.0f ? -1.0f : 1.0f;
}
} // namespace

GameObject
findOrThrow(VOG::Engine::Scene& scene, std::string_view name)
{
    if (const auto found = scene.find(name))
    {
        return *found;
    }

    throw std::logic_error{std::format("The game has no object called \"{}\".", name)};
}

Paddle::Paddle(PaddleKeys keys)
    : mKeys{keys}
{
}

void
Paddle::update(const VOG::Engine::FrameContext& frame)
{
    GameObject object = getGameObject();
    auto&      state  = object.getComponent<PaddleState>();
    Transform& where  = object.getTransform();

    const float direction = static_cast<float>(frame.input.isDown(mKeys.upKey)) -
                            static_cast<float>(frame.input.isDown(mKeys.downKey));
    const float limit     = gCourtHalfHeight - state.halfHeight;
    where.position.y      = std::clamp(
        where.position.y + (direction * gPaddleSpeed * frame.deltaSeconds), -limit, limit);
    where.scale.y = state.halfHeight;
}

void
Bumper::update(const VOG::Engine::FrameContext& frame)
{
    GameObject object = getGameObject();
    auto&      state  = object.getComponent<BumperState>();
    if (state.drifting)
    {
        state.age += frame.deltaSeconds;
    }
    // A cosine: at age 0 it is at the top, and it is slowest there.
    object.getTransform().position.y =
        gBumperAmplitude * std::cos(gTwoPi * state.age / gBumperPeriodSeconds);
}

Ball::Ball(VOG::Engine::Mesh ghostMesh, VOG::Engine::Material ghostMaterial)
    : mGhostMesh{std::move(ghostMesh)}
    , mGhostMaterial{std::move(ghostMaterial)}
{
}

void
Ball::start()
{
    VOG::Engine::Scene& scene = getScene();
    mLeft                     = findOrThrow(scene, "left paddle");
    mRight                    = findOrThrow(scene, "right paddle");
    mBumper                   = findOrThrow(scene, "bumper");
    mReferee                  = findOrThrow(scene, "referee");
}

void
Ball::serve()
{
    mPhase                                       = Phase::eFlying;
    mBumper.getComponent<BumperState>().drifting = true;
    mSpeed                                       = gStartSpeed;
    // Toward the side that missed, with a slant that changes sides at each serve.
    mSlantUp          = !mSlantUp;
    const float slant = mSlantUp ? gServeSlant : -gServeSlant;
    mVelocity =
        VOG::Math::Vector2f{mServeToward, slant} * (mSpeed / std::sqrt(1.0f + (slant * slant)));
}

bool
Ball::hits(GameObject paddle)
{
    const Transform& ball  = getGameObject().getTransform();
    Transform&       other = paddle.getTransform();

    return std::abs(ball.position.x - other.position.x) < gPaddleHalfWidth + gBallHalfSize &&
           std::abs(ball.position.y - other.position.y) < other.scale.y + gBallHalfSize;
}

void
Ball::bounceOff(GameObject paddle, float direction)
{
    Transform& ball  = getGameObject().getTransform();
    Transform& other = paddle.getTransform();
    auto&      state = paddle.getComponent<PaddleState>();

    // Out of the paddle, then away from it, steeper the farther from its middle the ball hit.
    ball.position.x = other.position.x + (direction * (gPaddleHalfWidth + gBallHalfSize));
    const float offset =
        std::clamp((ball.position.y - other.position.y) / other.scale.y, -1.0f, 1.0f);
    mSpeed = std::min(mSpeed * gSpeedUpPerHit, gMaxSpeed);
    const VOG::Math::Vector2f heading{direction, offset * gSteepness};
    mVelocity = glm::normalize(heading) * mSpeed;

    state.halfHeight = std::max(state.halfHeight * gShrinkPerHit, gMinHalfHeight);
}

void
Ball::reboundOffBumper()
{
    Transform&       ball   = getGameObject().getTransform();
    const Transform& bumper = mBumper.getTransform();

    const float across = ball.position.x - bumper.position.x;
    const float along  = ball.position.y - bumper.position.y;
    // How far the ball is inside the bumper, on each axis.
    const float insideX = gBumperHalfWidth + gBallHalfSize - std::abs(across);
    const float insideY = gBumperHalfHeight + gBallHalfSize - std::abs(along);
    if (insideX <= 0.0f || insideY <= 0.0f)
    {
        return;
    }

    // Out through the nearer side, and away from the bumper on that axis.
    if (insideX < insideY)
    {
        const float away = sign(across);
        ball.position.x  = bumper.position.x + (away * (gBumperHalfWidth + gBallHalfSize));
        mVelocity.x      = away * std::abs(mVelocity.x);
    }
    else
    {
        const float away = sign(along);
        ball.position.y  = bumper.position.y + (away * (gBumperHalfHeight + gBallHalfSize));
        mVelocity.y      = away * std::abs(mVelocity.y);
    }
}

void
Ball::score(Side scorer)
{
    auto& points = mReferee.getComponent<Score>();
    (scorer == Side::eLeft ? points.left : points.right) += 1;
    points.matchOver = std::max(points.left, points.right) >= gPointsToWin;
    std::cout << "Rally: " << points.left << " - " << points.right
              << (points.matchOver ? "  (match over: space starts the next)" : "") << '\n';

    for (GameObject* paddle : {&mLeft, &mRight})
    {
        paddle->getComponent<PaddleState>().halfHeight = gPaddleStartHalfHeight;
    }

    Transform& ball                     = getGameObject().getTransform();
    ball.position                       = VOG::Math::Vector3f{0.0f, 0.0f, ball.position.z};
    mPhase                              = Phase::eWaiting;
    mBumper.getComponent<BumperState>() = BumperState{};
    // The side that missed receives the next ball.
    mServeToward = scorer == Side::eLeft ? 1.0f : -1.0f;
}

void
Ball::leaveGhost()
{
    VOG::Engine::Scene& scene = getScene();
    GameObject          ghost = scene.createGameObject("ghost");

    const Transform& ball  = getGameObject().getTransform();
    Transform&       trail = ghost.getTransform();
    trail.position         = VOG::Math::Vector3f{ball.position.x, ball.position.y, gGhostDepth};
    trail.scale            = ball.scale * gGhostSize;
    ghost.addComponent<MeshRenderer>(MeshRenderer{.mesh = mGhostMesh, .material = mGhostMaterial});
    ghost.addBehaviour<Ghost>(gGhostSeconds);
}

void
Ball::step(float seconds)
{
    Transform& ball = getGameObject().getTransform();

    // Short steps, so that a fast ball cannot pass through a paddle between two of them.
    const int   steps = std::max(1, static_cast<int>(std::ceil(mSpeed * seconds / gMaxStepLength)));
    const float piece = seconds / static_cast<float>(steps);
    for (int i = 0; i < steps && mPhase == Phase::eFlying; ++i)
    {
        ball.position.x += mVelocity.x * piece;
        ball.position.y += mVelocity.y * piece;

        const float wall = gCourtHalfHeight - gBallHalfSize;
        if (std::abs(ball.position.y) > wall)
        {
            ball.position.y = sign(ball.position.y) * wall;
            mVelocity.y     = -mVelocity.y;
        }
        reboundOffBumper();
        if (mVelocity.x < 0.0f && hits(mLeft))
        {
            bounceOff(mLeft, 1.0f);
        }
        else if (mVelocity.x > 0.0f && hits(mRight))
        {
            bounceOff(mRight, -1.0f);
        }
        else if (std::abs(ball.position.x) > gCourtHalfWidth + gScoreMargin)
        {
            score(ball.position.x > 0.0f ? Side::eLeft : Side::eRight);
        }
    }
}

void
Ball::update(const VOG::Engine::FrameContext& frame)
{
    if (mPhase == Phase::eWaiting)
    {
        if (frame.input.wasPressed(Key::eSpace))
        {
            auto& points = mReferee.getComponent<Score>();
            if (points.matchOver)
            {
                points = Score{};
            }
            serve();
        }

        return;
    }

    step(frame.deltaSeconds);
    mGhostTimer += frame.deltaSeconds;
    if (mGhostTimer >= gGhostEvery && mPhase == Phase::eFlying)
    {
        mGhostTimer = 0.0f;
        leaveGhost();
    }
}

Ghost::Ghost(float seconds)
    : mSeconds{seconds}
{
}

void
Ghost::start()
{
    mStartScale = getGameObject().getTransform().scale;
}

void
Ghost::update(const VOG::Engine::FrameContext& frame)
{
    mAge += frame.deltaSeconds;
    if (mAge >= mSeconds)
    {
        getGameObject().destroy();

        return;
    }
    getGameObject().getTransform().scale = mStartScale * (1.0f - (mAge / mSeconds));
}

Pip::Pip(Side side, int number, VOG::Engine::Material lit, VOG::Engine::Material dark)
    : mSide{side}
    , mNumber{number}
    , mLit{std::move(lit)}
    , mDark{std::move(dark)}
{
}

void
Pip::start()
{
    mReferee = findOrThrow(getScene(), "referee");
}

void
Pip::update(const VOG::Engine::FrameContext& /*frame*/)
{
    const auto& points = mReferee.getComponent<Score>();
    const int   mine   = mSide == Side::eLeft ? points.left : points.right;

    getGameObject().getComponent<MeshRenderer>().material = mNumber <= mine ? mLit : mDark;
}

Referee::Referee(int maxFrames)
    : mMaxFrames{maxFrames}
{
}

void
Referee::update(const VOG::Engine::FrameContext& frame)
{
    ++mFrames;
    if (frame.input.wasPressed(Key::eEscape) || (mMaxFrames > 0 && mFrames >= mMaxFrames))
    {
        getScene().requestQuit();
    }
}
} // namespace Rally
