#pragma once

#include <VOG/Engine/Renderer/Material.hpp>
#include <VOG/Engine/Renderer/Mesh.hpp>
#include <VOG/Engine/Scene/Scene.hpp>

namespace Rally
{
/** What the game is drawn with. In a test every handle may be empty: nothing is drawn then. */
struct Look
{
    VOG::Engine::Mesh     quad;
    VOG::Engine::Material court;
    VOG::Engine::Material left;
    VOG::Engine::Material right;
    VOG::Engine::Material bumper;
    VOG::Engine::Material ball;
    VOG::Engine::Material ghost;
    VOG::Engine::Material lit;
    VOG::Engine::Material dark;
};

/** Puts the objects of the game into @p scene: the camera, the court, the paddles, the bumper,
 *  the ball, the referee and the marks of the score. The referee quits after @p maxFrames frames; 0
 * is never. */
void buildGame(VOG::Engine::Scene& scene, const Look& look, int maxFrames);
} // namespace Rally
