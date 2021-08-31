#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace VOG::Engine
{
class Renderer;
}

namespace VOG::Scene
{
class SceneObject;

class Scene
{
public:
    using SceneObjectPtr = std::shared_ptr<SceneObject>;

    Scene();

    ~Scene();

    bool addSceneObject(const std::string& name, const SceneObjectPtr& sceneObject);

    void drawScene(Engine::Renderer& renderer);

protected:
    std::unordered_map<std::string, SceneObjectPtr> m_sceneObjects;
};
} // namespace VOG::Scene