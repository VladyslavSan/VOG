#pragma once

#include <VOG/Common/Assert.hpp>
#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Scene/Components.hpp>
#include <VOG/Engine/Scene/Detail/Registry.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace VOG::Engine
{
class Scene;

class Behaviour;

/**
 * What a type has to be to be a component: an object type that is not const and does not throw
 * when it is destroyed (the Scene destroys components in the middle of its own changes).
 */
template <class T>
concept Component = std::is_object_v<T> && !std::is_const_v<T> && std::is_nothrow_destructible_v<T>;

/**
 * A handle to an object of a Scene. It owns nothing and must not outlive the Scene. An object is
 * valid from Scene::createGameObject() until the flush that ends the Scene::update() after
 * destroy(), or until Scene::clear(); after destroy() and before the flush it is still valid.
 *
 * On an invalid object, calls that can report absence do (isValid() is false, hasComponent() is
 * false, removeComponent() and destroy() do nothing); calls that must return or create something
 * throw std::logic_error. A default-constructed object is invalid.
 *
 * A component reference stays valid until that component is removed or its object is flushed.
 * Inside Scene::each(), a component that removeComponent() removes, and the components of objects
 * that update() or clear() flush, stay valid until the outermost each() returns.
 */
class GameObject final
{
public:
    GameObject() = default;

    /** Whether the object exists in its Scene. Main thread, like the Scene. */
    bool isValid() const;

    /**
     * Stops the behaviours of this object now and destroys the object at the end of the next
     * Scene::update(), after their onDestroy(). Safe inside Scene::each().
     * @throws std::bad_alloc if memory runs out. Nothing has changed then: the object lives and
     *         destroy() can be called again.
     */
    void destroy();

    /** @throws std::logic_error if the object is invalid. */
    Transform& getTransform();

    /**
     * @throws std::logic_error if the object is invalid or has a T; for a Camera, also if the
     *         Scene has one already.
     */
    template <Component T, class... Args>
    T& addComponent(Args&&... args);

    /** Whether the object has a T. False for an invalid object. */
    template <Component T>
    bool hasComponent() const noexcept;

    /** @throws std::logic_error if the object is invalid or has no T: ask hasComponent() first. */
    template <Component T>
    T& getComponent();

    /** Not for Transform or Name: every object has them. */
    template <Component T>
    void removeComponent();

    /**
     * Adds a T, which starts at the next Scene::update(). Added to an object that is destroyed
     * before then, it never starts and gets no onDestroy().
     * @throws std::logic_error if the object is invalid.
     */
    template <std::derived_from<Behaviour> T, class... Args>
    T& addBehaviour(Args&&... args);

    bool operator==(const GameObject&) const = default;

private:
    friend class Scene;

    GameObject(Scene& scene, Detail::Entity entity);

    Scene*         mScene = nullptr;
    Detail::Entity mEntity;
};

/**
 * A script on a GameObject. Its constructor cannot reach its object or its Scene; start() can.
 * Its functions run on the main thread inside Application::run(),
 * possibly inside the event watch of a live resize: they must not call Renderer::submitFrame() or
 * Renderer::trySubmitFrame(), and must not wait for a thread that pushes SDL events.
 *
 * start() and update() may throw: the exception ends Application::run(). onDestroy() exceptions are
 * logged. The destructor of a Behaviour, like that of a component, must not call into its Scene:
 * the Scene destroys it in the middle of its own change.
 */
class Behaviour
{
public:
    Behaviour()          = default;
    virtual ~Behaviour() = default;

    // A copy would carry the state and the object of the original (C.67).
    Behaviour(const Behaviour&)            = delete;
    Behaviour(Behaviour&&)                 = delete;
    Behaviour& operator=(const Behaviour&) = delete;
    Behaviour& operator=(Behaviour&&)      = delete;

    /** Before the first update(). Runs once, also if it throws. */
    virtual void
    start()
    {
    }

    virtual void
    update(const FrameContext& /*frame*/)
    {
    }

    /** Only for a behaviour that started: when its object is flushed, or by Scene::clear() and
     *  the destruction of the Scene. */
    virtual void
    onDestroy()
    {
    }

    /** Invalid in the constructor. */
    GameObject
    getGameObject() const
    {
        return mGameObject;
    }

    /** The Scene of the object, to find and create objects. Main thread.
     *  @throws std::logic_error in the constructor, where the behaviour has no object yet. */
    Scene& getScene() const;

private:
    friend class Scene;

    GameObject mGameObject;
    Scene*     mScene = nullptr;
    /** Orders the behaviours of a Scene by the time they were added. */
    std::uint64_t mBirth = 0u;
};

/**
 * The objects of a world. Main thread only: the thread that made the Scene is its thread, and in a
 * debug build a call from another one aborts. That holds for every public member of the Scene and
 * of GameObject that reads or writes the state of the Scene, isValid(), hasComponent(), each() and
 * the quit requests included. It does not hold for copying and comparing a GameObject, or for its
 * destructor, which read the handle and nothing else, nor for getGameObject() and getScene() of a
 * Behaviour, which read the Behaviour. The destructors of its components and behaviours must not
 * call into the Scene. Application calls update(), then submits a frame, whose onDraw() extracts
 * the draws and the camera.
 */
class Scene final : public Renderable
{
public:
    Scene() = default;

    /** Calls clear(); never throws. */
    ~Scene() override;

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    /**
     * An object with a Transform and a Name. The name is for find() and for the error that names a
     * draw; it need not be unique.
     * @throws std::bad_alloc if memory runs out. Nothing has changed then: no object was made.
     */
    GameObject createGameObject(std::string name = {});

    /**
     * The first object with this name, or nothing. "First" is the order of each(): that of
     * creation, until objects are destroyed and their places are used again. An object that
     * destroy() marked is not found.
     */
    [[nodiscard]] std::optional<GameObject> find(std::string_view name);

    /**
     * Asks Application::run() to return after the frame that called it. Main thread, from a
     * Behaviour or a posted task.
     */
    void
    requestQuit() noexcept
    {
        requireOwnerThread();
        mQuitRequested = true;
    }

    [[nodiscard]] bool
    quitRequested() const noexcept
    {
        requireOwnerThread();

        return mQuitRequested;
    }

    /** Forgets a request: Application::run() does it when it returns, so that the next run() runs.
     */
    void
    clearQuitRequest() noexcept
    {
        requireOwnerThread();
        mQuitRequested = false;
    }

    /**
     * Starts the behaviours added since the last update, updates the started ones, then destroys
     * the objects that destroy() marked, each with onDestroy() of its started behaviours first.
     * An exception from start() or update() leaves the flush for the next update.
     * @throws std::logic_error before it changes anything, if called from start(), update(),
     *         onDestroy() or clear() of this Scene.
     */
    void update(const FrameContext& frame);

    /**
     * Destroys every object now, with onDestroy() of its started behaviours, and invalidates every
     * GameObject. The same throw rule as update().
     */
    void clear();

    /**
     * Calls visitor(GameObject, First&, Rest&...) for every object that has all the types,
     * including objects marked by destroy(). It walks the First components present when it starts
     * and looks the other types up at each visit, so a change to an object it has not reached yet
     * counts. An object that visitor creates, or a First component it adds, is not visited by this
     * call. A component that visitor removes is destroyed when the outermost each() returns.
     */
    template <class First, class... Rest, class F>
    void
    each(F&& visitor)
    {
        requireOwnerThread();
        mRegistry.each<First, Rest...>(
            [&](Detail::Entity entity, First& first, Rest&... rest)
            {
                visitor(GameObject{*this, entity}, first, rest...);
            });
    }

    void onDraw(const FrameContext& frame, RenderQueue& queue) override;

private:
    friend class GameObject;
    /** Lets SceneTest see which slots the registry hands out. */
    friend struct SceneTestAccess;

    class BusyScope;

    /** Marks an entity whose GameObject::destroy() ran and whose flush has not. Never walked by
     * each(). */
    struct PendingDestroy
    {
    };

    /** Binds @p behaviour to @p object and appends it to the scripts of the object; it starts at
     *  the next update(). */
    void adopt(GameObject object, std::unique_ptr<Behaviour> behaviour);

    void markDestroyed(Detail::Entity entity);

    bool isDying(Detail::Entity entity);

    /** Calls onDestroy() of a behaviour that started; the exception is logged. */
    static void retire(Behaviour& behaviour);

    void flush();

    /** Aborts, in a debug build, when the calling thread is not the one that made the Scene. */
    void
    requireOwnerThread() const
    {
        VOG_ASSERT_MSG(std::this_thread::get_id() == mOwner,
                       "A Scene belongs to the thread that made it.");
    }

    Detail::Registry            mRegistry;
    std::vector<Detail::Entity> mDestroyQueue;
    /** The birth of the next behaviour. */
    std::uint64_t mNextBirth = 0u;
    /** Inside update() or clear(). */
    bool mBusy          = false;
    bool mQuitRequested = false;

    const std::thread::id mOwner = std::this_thread::get_id();
};

template <Component T, class... Args>
T&
GameObject::addComponent(Args&&... args)
{
    if (!isValid())
    {
        throw std::logic_error{"Cannot add a component to an invalid GameObject."};
    }
    if constexpr (std::is_same_v<T, Camera>)
    {
        if (mScene->mRegistry.size<Camera>() != 0)
        {
            throw std::logic_error{"A Scene has at most one Camera."};
        }
    }

    return mScene->mRegistry.emplace<T>(mEntity, std::forward<Args>(args)...);
}

template <Component T>
bool
GameObject::hasComponent() const noexcept
{
    if (mScene == nullptr)
    {
        return false;
    }
    mScene->requireOwnerThread();

    return mScene->mRegistry.tryGet<T>(mEntity) != nullptr;
}

template <Component T>
T&
GameObject::getComponent()
{
    if (!isValid())
    {
        throw std::logic_error{"An invalid GameObject has no components."};
    }
    T* component = mScene->mRegistry.tryGet<T>(mEntity);
    if (component == nullptr)
    {
        throw std::logic_error{"The GameObject has no such component; ask hasComponent() first."};
    }

    return *component;
}

template <Component T>
void
GameObject::removeComponent()
{
    static_assert(!std::is_same_v<T, Transform> && !std::is_same_v<T, Name>,
                  "Every GameObject keeps its Transform and its Name.");
    if (isValid())
    {
        mScene->mRegistry.remove<T>(mEntity);
    }
}

template <std::derived_from<Behaviour> T, class... Args>
T&
GameObject::addBehaviour(Args&&... args)
{
    if (!isValid())
    {
        throw std::logic_error{"Cannot add a behaviour to an invalid GameObject."};
    }

    auto behaviour = std::make_unique<T>(std::forward<Args>(args)...);
    T&   result    = *behaviour;
    mScene->adopt(*this, std::move(behaviour));

    return result;
}
} // namespace VOG::Engine
