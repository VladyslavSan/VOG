#include "VOG/Engine/Scene/Scene.hpp"

#include <VOG/Engine/Scene/SceneObject.hpp>

namespace VOG::Scene
{
Scene::Scene() {}

Scene::~Scene() {}

bool
Scene::addSceneObject(const std::string& name, const Scene::SceneObjectPtr& sceneObject)
{
    auto inserted = m_sceneObjects.emplace(
        std::piecewise_construct, std::forward_as_tuple(name), std::forward_as_tuple(sceneObject));

    return inserted.second;
}

void
Scene::drawScene(Engine::Renderer& renderer)
{
    for (auto& sceneObject : m_sceneObjects)
    {
        sceneObject.second->draw(renderer);
    }
}
} // namespace VOG::Scene
