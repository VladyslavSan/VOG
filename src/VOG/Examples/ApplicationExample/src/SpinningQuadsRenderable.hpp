#pragma once

#include <VOG/Engine/Renderer/Renderable.hpp>

namespace VOG::Engine
{
class Renderer;
} // namespace VOG::Engine

/**
 * Two colored quads orbiting the screen center — the demo that used to be hardcoded
 * inside Renderer::render().
 */
class SpinningQuadsRenderable final : public VOG::Engine::Renderable
{
public:
    explicit SpinningQuadsRenderable(VOG::Engine::Renderer& renderer);

    void onDraw(const VOG::Engine::FrameContext& frameContext,
                VOG::Engine::RenderQueue&        queue) override;

private:
    VOG::Engine::MeshPtr     mMesh;
    VOG::Engine::MaterialPtr mMaterial;
};
