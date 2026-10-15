#pragma once

#include <VOG/Engine/Renderer/FrameContext.hpp>
#include <VOG/Engine/Renderer/Renderable.hpp>
#include <VOG/Engine/Scene/Components.hpp>
#include <VOG/Engine/Scene/Registry.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace VOG::Engine
{
class Scene;

/**
 * A handle to an object of a Scene. It owns nothing and must not outlive the Scene. An object is
 * valid from Scene::createGameObject() until the flush that ends the Scene::update() after
 * destroy(), or until Scene::clear(); after destroy() and before the flush it is still valid.
 *
 * On an invalid object, calls that can report absence do (isValid() is false, getComponent()
 * returns nullptr, removeComponent() and destroy() do nothing); calls that must return or create
 * something throw std::logic_error. A default-constructed object is invalid.
 *
 * A component reference stays valid until that component is removed or its object is flushed.
 */
class GameObject final
{
public:
    GameObject() = default;

    bool isValid() const;

    /**
     * Stops the behaviours of this object now and destroys the object at the end of the next
     * Scene::update(), after their onDestroy(). Safe inside Scene::each().
     */
    void destroy();

    /** @throws std::logic_error if the object is invalid. */
    Transform& getTransform();

    /**
     * @throws std::logic_error if the object is invalid or has a T; for a Camera, also if the
     *         Scene has one already; if T is queried by a running Scene::each().
     */
    template <class T, class... Args>
    T& addComponent(Args&&... args);

    /** The T of this object, or nullptr if it has none. */
    template <class T>
    T* getComponent();

    /** Not for Transform: every object has one. @throws std::logic_error if T is queried by a
     *  running Scene::each(). */
    template <class T>
    void removeComponent();

    /**
     * Adds a T, which starts at the next Scene::update(). Added to an object that is destroyed
     * before then, it never starts and gets no onDestroy().
     * @throws std::logic_error if the object is invalid.
     */
    template <class T, class... Args>
    T& addBehaviour(Args&&... args);

    /** @throws std::logic_error for a default-constructed object. */
    Scene& getScene() const;

    bool operator==(const GameObject&) const = default;

private:
    friend class Scene;

    GameObject(Scene& scene, Detail::Entity entity);

    Scene*         mScene = nullptr;
    Detail::Entity mEntity;
};

/**
 * A script on a GameObject. Its functions run on the main thread inside Application::run(),
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
    virtual ~Behaviour() = default;

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

private:
    friend class Scene;

    enum class State : std::uint8_t
    {
        ePending,
        eStarted,
        eRemoved
    };

    GameObject mGameObject;
    State      mState = State::ePending;
};

/**
 * The objects of a world. Main thread only. The destructors of its components and behaviours must
 * not call into the Scene. Application calls update(), then submits a frame,
 * whose onDraw() extracts the draws and the camera.
 */
class Scene final : public Renderable
{
public:
    Scene() = default;

    /** Calls clear(); never throws. */
    ~Scene() override;

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    /** An object with a Transform. @throws std::logic_error inside each<Transform...>. */
    GameObject createGameObject();

    /**
     * Starts the behaviours added since the last update, updates the started ones, then destroys
     * the objects that destroy() marked, each with onDestroy() of its started behaviours first.
     * An exception from start() or update() leaves the flush for the next update.
     * @throws std::logic_error before it changes anything, if called from start(), update(),
     *         onDestroy() or clear() of this Scene or from inside each().
     */
    void update(const FrameContext& frame);

    /**
     * Destroys every object now, with onDestroy() of its started behaviours, and invalidates every
     * GameObject. The same throw rule as update().
     */
    void clear();

    /** Calls visitor(GameObject, First&, Rest&...) for every object that has all the types,
     * including objects marked by destroy(). Adding or removing a queried type inside visitor
     * throws. */
    template <class First, class... Rest, class F>
    void
    each(F&& visitor)
    {
        mRegistry.each<First, Rest...>(
            [&](Detail::Entity entity, First& first, Rest&... rest)
            {
                visitor(GameObject{*this, entity}, first, rest...);
            });
    }

    void onDraw(const FrameContext& frame, RenderQueue& queue) override;

private:
    friend class GameObject;

    class BusyScope;

    /** Binds @p behaviour to @p object and appends it; it starts at the next update(). */
    void adopt(GameObject object, std::unique_ptr<Behaviour> behaviour);

    void markDestroyed(Detail::Entity entity);

    bool isDying(const Behaviour& behaviour);

    /** Stops @p behaviour and calls onDestroy() if it had started; the exception is logged. */
    static void retire(Behaviour& behaviour);

    void flush();

    Detail::Registry                        mRegistry;
    std::vector<std::unique_ptr<Behaviour>> mBehaviours;
    std::vector<Detail::Entity>             mDestroyQueue;
    /** Inside update() or clear(). */
    bool mBusy = false;
};

template <class T, class... Args>
T&
GameObject::addComponent(Args&&... args)
{
    static_assert(!std::is_same_v<T, Detail::PendingDestroy>);
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

template <class T>
T*
GameObject::getComponent()
{
    return mScene != nullptr ? mScene->mRegistry.tryGet<T>(mEntity) : nullptr;
}

template <class T>
void
GameObject::removeComponent()
{
    static_assert(!std::is_same_v<T, Transform>, "Every GameObject keeps its Transform.");
    static_assert(!std::is_same_v<T, Detail::PendingDestroy>);
    if (isValid())
    {
        mScene->mRegistry.remove<T>(mEntity);
    }
}

template <class T, class... Args>
T&
GameObject::addBehaviour(Args&&... args)
{
    static_assert(std::derived_from<T, Behaviour>);
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
