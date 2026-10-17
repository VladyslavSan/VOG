#include "VOG/Engine/Scene/Scene.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace VOG::Engine
{
namespace
{
/** Logs an exception that cannot go on, worded like Renderer::~Renderer. */
void
logOnDestroyFailure()
{
    try
    {
        throw;
    }
    catch (const std::exception& exception)
    {
        spdlog::error("Behaviour::onDestroy() threw: {}", exception.what());
    }
    catch (...)
    {
        spdlog::error("Behaviour::onDestroy() threw an unknown exception.");
    }
}
} // namespace

/** Holds mBusy for its lifetime; the entry check of update() and clear(). */
class Scene::BusyScope
{
public:
    explicit BusyScope(Scene& scene)
        : mScene{scene}
    {
        if (scene.mBusy)
        {
            throw std::logic_error{
                "Scene::update() and Scene::clear() cannot run while either "
                "already runs on the Scene."};
        }
        mScene.mBusy = true;
    }

    ~BusyScope() { mScene.mBusy = false; }

    BusyScope(const BusyScope&)            = delete;
    BusyScope& operator=(const BusyScope&) = delete;

private:
    Scene& mScene;
};

GameObject::GameObject(Scene& scene, Detail::Entity entity)
    : mScene{&scene}
    , mEntity{entity}
{
}

bool
GameObject::isValid() const
{
    return mScene != nullptr && mScene->mRegistry.isAlive(mEntity);
}

void
GameObject::destroy()
{
    if (isValid())
    {
        mScene->markDestroyed(mEntity);
    }
}

Transform&
GameObject::getTransform()
{
    if (!isValid())
    {
        throw std::logic_error{"An invalid GameObject has no Transform."};
    }

    return *mScene->mRegistry.tryGet<Transform>(mEntity);
}

Scene&
GameObject::getScene() const
{
    if (mScene == nullptr)
    {
        throw std::logic_error{"A default-constructed GameObject has no Scene."};
    }

    return *mScene;
}

Scene::~Scene()
{
    try
    {
        clear();
    }
    catch (...)
    {
        spdlog::error("The Scene could not clear itself while it was destroyed.");
    }
}

GameObject
Scene::createGameObject()
{
    const Detail::Entity entity = mRegistry.create();
    mRegistry.emplace<Transform>(entity);

    return {*this, entity};
}

void
Scene::adopt(GameObject object, std::unique_ptr<Behaviour> behaviour)
{
    behaviour->mGameObject = object;
    mBehaviours.push_back(std::move(behaviour));
}

void
Scene::markDestroyed(Detail::Entity entity)
{
    if (mRegistry.tryGet<PendingDestroy>(entity) == nullptr)
    {
        mRegistry.emplace<PendingDestroy>(entity);
        mDestroyQueue.push_back(entity);
    }
}

bool
Scene::isDying(const Behaviour& behaviour)
{
    return mRegistry.tryGet<PendingDestroy>(behaviour.mGameObject.mEntity) != nullptr;
}

void
Scene::retire(Behaviour& behaviour)
{
    const bool started = behaviour.mState == Behaviour::State::eStarted;
    behaviour.mState   = Behaviour::State::eRemoved;
    if (!started)
    {
        return;
    }

    try
    {
        behaviour.onDestroy();
    }
    catch (...)
    {
        logOnDestroyFailure();
    }
}

void
Scene::update(const FrameContext& frame)
{
    const BusyScope busy{*this};

    // Every pass indexes mBehaviours afresh: user code may append to it. Nothing erases from it
    // before the end of flush(), so a Behaviour& stays valid while its own function runs.
    // A behaviour added by start() or update() is past the bound and pending: it starts at the
    // next update().
    const std::size_t count = mBehaviours.size();
    for (std::size_t i = 0; i < count; ++i)
    {
        Behaviour& behaviour = *mBehaviours[i];
        if (behaviour.mState == Behaviour::State::ePending && !isDying(behaviour))
        {
            // Before start(): a start() that throws is not run again.
            behaviour.mState = Behaviour::State::eStarted;
            behaviour.start();
        }
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        Behaviour& behaviour = *mBehaviours[i];
        if (behaviour.mState == Behaviour::State::eStarted && !isDying(behaviour))
        {
            behaviour.update(frame);
        }
    }

    flush();
}

void
Scene::flush()
{
    // Ends: only a started behaviour runs onDestroy(), each behaviour is removed once, and one
    // created by onDestroy() is pending, so it is removed without a call.
    while (!mDestroyQueue.empty())
    {
        const std::vector<Detail::Entity> queue = std::exchange(mDestroyQueue, {});
        for (const Detail::Entity entity : queue)
        {
            // Size re-read: onDestroy() may add behaviours.
            for (std::size_t i = 0; i < mBehaviours.size(); ++i)
            {
                Behaviour& behaviour = *mBehaviours[i];
                if (behaviour.mState != Behaviour::State::eRemoved &&
                    behaviour.mGameObject.mEntity == entity)
                {
                    retire(behaviour);
                }
            }
            mRegistry.destroy(entity);
        }
    }

    std::erase_if(mBehaviours,
                  [](const std::unique_ptr<Behaviour>& behaviour)
                  {
                      return behaviour->mState == Behaviour::State::eRemoved;
                  });
}

void
Scene::clear()
{
    const BusyScope busy{*this};

    for (std::size_t i = 0; i < mBehaviours.size(); ++i)
    {
        if (mBehaviours[i]->mState != Behaviour::State::eRemoved)
        {
            retire(*mBehaviours[i]);
        }
    }
    // After the loop: onDestroy() may have created objects.
    mRegistry.destroyAll();
    mBehaviours.clear();
    mDestroyQueue.clear();
}

void
Scene::onDraw(const FrameContext& /*frame*/, RenderQueue& queue)
{
    mRegistry.each<MeshRenderer, Transform>(
        [&queue](Detail::Entity /*entity*/, MeshRenderer& renderer, Transform& transform)
        {
            queue.submit(renderer.mesh, renderer.material, transform.matrix());
        });

    // At most one Camera exists (GameObject::addComponent), so this sets the view at most once.
    mRegistry.each<Camera, Transform>(
        [&queue](Detail::Entity /*entity*/, Camera& camera, Transform& transform)
        {
            queue.setCamera({.position  = transform.position,
                             .direction = transform.rotation * Math::Vector3f{0.0f, 0.0f, 1.0f},
                             .up        = transform.rotation * Math::Vector3f{0.0f, 1.0f, 0.0f},
                             .nearPlane = camera.nearPlane,
                             .farPlane  = camera.farPlane});
        });
}
} // namespace VOG::Engine
