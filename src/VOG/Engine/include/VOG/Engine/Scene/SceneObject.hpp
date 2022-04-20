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
    virtual void prepare(Engine::Renderer& renderer) = 0;
    virtual void draw(Engine::Renderer& renderer)    = 0;
};
} // namespace VOG::Scene
