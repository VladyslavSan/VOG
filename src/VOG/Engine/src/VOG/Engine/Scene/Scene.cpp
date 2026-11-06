#include "VOG/Engine/Scene/Scene.hpp"

#include <spdlog/spdlog.h>

#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace VOG::Engine
{
namespace
{
/**
 * The behaviours of one object, in the order they started and the order they were added. Every
 * object that has a behaviour has this component, so the Scene walks the behaviours of an object
 * and not those of all objects.
 */
struct Scripts
{
    std::vector<std::unique_ptr<Behaviour>> started;
    std::vector<std::unique_ptr<Behaviour>> pending;
};

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
    if (mScene == nullptr)
    {
        return false;
    }
    mScene->requireOwnerThread();

    return mScene->mRegistry.isAlive(mEntity);
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

Scene&
Behaviour::getScene() const
{
    if (mScene == nullptr)
    {
        throw std::logic_error{
            "A Behaviour has no Scene in its constructor: use getScene() from start()."};
    }

    return *mScene;
}

GameObject
Scene::createGameObject(std::string name)
{
    requireOwnerThread();

    const Detail::Entity entity = mRegistry.create();
    try
    {
        mRegistry.emplace<Transform>(entity);
        mRegistry.emplace<Name>(entity, Name{.value = std::move(name)});
    }
    catch (...)
    {
        // Nobody can reach the entity without a GameObject: do not leave it alive.
        mRegistry.destroy(entity);
        throw;
    }

    return {*this, entity};
}

std::optional<GameObject>
Scene::find(std::string_view name)
{
    requireOwnerThread();

    std::optional<GameObject> found;
    mRegistry.each<Name>(
        [&](Detail::Entity entity, Name& candidate)
        {
            if (!found && candidate.value == name && !isDying(entity))
            {
                found = GameObject{*this, entity};
            }
        });

    return found;
}

void
Scene::adopt(GameObject object, std::unique_ptr<Behaviour> behaviour)
{
    requireOwnerThread();

    behaviour->mGameObject = object;
    behaviour->mScene      = this;
    behaviour->mBirth      = mNextBirth;

    Scripts* scripts = mRegistry.tryGet<Scripts>(object.mEntity);
    if (scripts == nullptr)
    {
        scripts = &mRegistry.emplace<Scripts>(object.mEntity);
    }
    scripts->pending.push_back(std::move(behaviour));
    ++mNextBirth;
}

void
Scene::markDestroyed(Detail::Entity entity)
{
    if (mRegistry.tryGet<PendingDestroy>(entity) == nullptr)
    {
        // The queue first: a push_back that threw after the marker was set would leave an object
        // that is marked and never flushed, and no later destroy() would act on it.
        mDestroyQueue.push_back(entity);
        try
        {
            mRegistry.emplace<PendingDestroy>(entity);
        }
        catch (...)
        {
            mDestroyQueue.pop_back();
            throw;
        }
    }
}

bool
Scene::isDying(Detail::Entity entity)
{
    return mRegistry.tryGet<PendingDestroy>(entity) != nullptr;
}

void
Scene::retire(Behaviour& behaviour)
{
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
    requireOwnerThread();
    const BusyScope busy{*this};

    // A behaviour that start() or update() adds is born at or after this limit: it is pending and
    // starts at the next update(), also on an object that this walk has not reached yet.
    const std::uint64_t limit = mNextBirth;

    mRegistry.each<Scripts>(
        [&](Detail::Entity entity, Scripts& scripts)
        {
            while (!scripts.pending.empty() && scripts.pending.front()->mBirth < limit &&
                   !isDying(entity))
            {
                // Moved before start(): a start() that throws is not run again, and the behaviours
                // behind it stay pending.
                scripts.started.push_back(std::move(scripts.pending.front()));
                scripts.pending.erase(scripts.pending.begin());
                scripts.started.back()->start();
            }
        });
    mRegistry.each<Scripts>(
        [&](Detail::Entity entity, Scripts& scripts)
        {
            // Nothing adds to `started` while behaviours update: new behaviours are pending.
            for (const std::unique_ptr<Behaviour>& behaviour : scripts.started)
            {
                if (isDying(entity))
                {
                    break;
                }
                behaviour->update(frame);
            }
        });

    flush();
}

void
Scene::flush()
{
    // Ends: only a started behaviour runs onDestroy(), once, with the object, and one that
    // onDestroy() adds is pending, so it is dropped with the object, without a call.
    while (!mDestroyQueue.empty())
    {
        const std::vector<Detail::Entity> queue = std::exchange(mDestroyQueue, {});
        for (const Detail::Entity entity : queue)
        {
            if (const Scripts* scripts = mRegistry.tryGet<Scripts>(entity))
            {
                for (const std::unique_ptr<Behaviour>& behaviour : scripts->started)
                {
                    retire(*behaviour);
                }
            }
            mRegistry.destroy(entity);
        }
    }
}

void
Scene::clear()
{
    requireOwnerThread();
    const BusyScope busy{*this};

    mRegistry.each<Scripts>(
        [](Detail::Entity /*entity*/, Scripts& scripts)
        {
            for (const std::unique_ptr<Behaviour>& behaviour : scripts.started)
            {
                retire(*behaviour);
            }
        });
    // After the walk: onDestroy() may have created objects.
    mRegistry.destroyAll();
    mDestroyQueue.clear();
}

void
Scene::onDraw(const FrameContext& /*frame*/, RenderQueue& queue)
{
    requireOwnerThread();

    mRegistry.each<MeshRenderer, Transform, Name>(
        [&queue](
            Detail::Entity /*entity*/, MeshRenderer& renderer, Transform& transform, Name& name)
        {
            // Not a debug name per draw: the DrawError of a draw that cannot go on is the only
            // one that is worth a string.
            try
            {
                queue.submit(renderer.mesh, renderer.material, transform.matrix());
            }
            catch (const DrawError& error)
            {
                throw DrawError{std::format("GameObject \"{}\": {}", name.value, error.what())};
            }
        });

    // At most one Camera exists (GameObject::addComponent), so this sets the view at most once.
    mRegistry.each<Camera, Transform>(
        [&queue](Detail::Entity /*entity*/, Camera& camera, Transform& transform)
        {
            queue.setCamera({.position   = transform.position,
                             .direction  = transform.rotation * Math::Vector3f{0.0f, 0.0f, 1.0f},
                             .up         = transform.rotation * Math::Vector3f{0.0f, 1.0f, 0.0f},
                             .projection = camera.projection,
                             .clearColor = camera.clearColor});
        });
}
} // namespace VOG::Engine
