#pragma once

namespace VOG::Engine
{
class Renderer;
}

namespace VOG::Scene
{
class SceneObject
{
public:
    virtual bool onDraw(Engine::Renderer& renderer) const = 0;
};
} // namespace VOG::Scene
