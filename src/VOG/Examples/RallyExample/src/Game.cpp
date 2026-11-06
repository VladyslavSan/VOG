#include "Game.hpp"

#include <VOG/Math/Math.hpp>

#include <format>
#include <string>
#include <utility>

#include "Behaviours.hpp"

namespace Rally
{
namespace
{
using VOG::Engine::GameObject;
using VOG::Engine::Key;

/** Far behind everything else, then the paddles and the bumper, the marks of the score, and the
 *  ball in front. */
constexpr float gCourtDepth = 10.0f;
constexpr float gBallDepth  = -1.0f;
constexpr float gPipDepth   = 0.0f;

constexpr float gPipRow     = 330.0f;
constexpr float gPipFirstX  = 350.0f;
constexpr float gPipSpacing = 34.0f;
constexpr float gPipHalf    = 11.0f;

constexpr VOG::Math::Vector4f gNightBlue{0.02f, 0.04f, 0.07f, 1.0f};

/** An object that draws the quad with a material, at a place and a size. */
GameObject
makeSprite(VOG::Engine::Scene&          scene,
           std::string                  name,
           const Look&                  look,
           const VOG::Engine::Material& material,
           const VOG::Math::Vector3f&   position,
           const VOG::Math::Vector2f&   halfSize)
{
    GameObject object              = scene.createGameObject(std::move(name));
    object.getTransform().position = position;
    object.getTransform().scale    = {halfSize.x, halfSize.y, 1.0f};
    object.addComponent<VOG::Engine::MeshRenderer>(
        VOG::Engine::MeshRenderer{.mesh = look.quad, .material = material});

    return object;
}
} // namespace

void
buildGame(VOG::Engine::Scene& scene, const Look& look, int maxFrames)
{
    scene.createGameObject("camera").addComponent<VOG::Engine::Camera>(VOG::Engine::Camera{
        .projection = VOG::Engine::Orthographic{.height = gCourtHeight}, .clearColor = gNightBlue});

    makeSprite(scene,
               "court",
               look,
               look.court,
               {0.0f, 0.0f, gCourtDepth},
               {gCourtHalfWidth, gCourtHalfHeight});

    const VOG::Math::Vector2f paddleHalf{gPaddleHalfWidth, gPaddleStartHalfHeight};
    GameObject                left =
        makeSprite(scene, "left paddle", look, look.left, {-gPaddleX, 0.0f, 0.0f}, paddleHalf);
    GameObject right =
        makeSprite(scene, "right paddle", look, look.right, {gPaddleX, 0.0f, 0.0f}, paddleHalf);
    left.addComponent<PaddleState>();
    right.addComponent<PaddleState>();
    left.addBehaviour<Paddle>(PaddleKeys{.upKey = Key::eW, .downKey = Key::eS});
    right.addBehaviour<Paddle>(PaddleKeys{.upKey = Key::eUp, .downKey = Key::eDown});

    GameObject bumper = makeSprite(scene,
                                   "bumper",
                                   look,
                                   look.bumper,
                                   {0.0f, gBumperAmplitude, 0.0f},
                                   {gBumperHalfWidth, gBumperHalfHeight});
    bumper.addComponent<BumperState>();
    bumper.addBehaviour<Bumper>();

    makeSprite(
        scene, "ball", look, look.ball, {0.0f, 0.0f, gBallDepth}, {gBallHalfSize, gBallHalfSize})
        .addBehaviour<Ball>(look.quad, look.ghost);

    GameObject referee = scene.createGameObject("referee");
    referee.addComponent<Score>();
    referee.addBehaviour<Referee>(maxFrames);

    // Seven marks for a side, from the middle of the top edge outward.
    for (int number = 1; number <= gPointsToWin; ++number)
    {
        const float offset = gPipFirstX + (static_cast<float>(number - 1) * gPipSpacing);
        makeSprite(scene,
                   std::format("left pip {}", number),
                   look,
                   look.dark,
                   {-offset, gPipRow, gPipDepth},
                   {gPipHalf, gPipHalf})
            .addBehaviour<Pip>(Side::eLeft, number, look.lit, look.dark);
        makeSprite(scene,
                   std::format("right pip {}", number),
                   look,
                   look.dark,
                   {offset, gPipRow, gPipDepth},
                   {gPipHalf, gPipHalf})
            .addBehaviour<Pip>(Side::eRight, number, look.lit, look.dark);
    }
}
} // namespace Rally
