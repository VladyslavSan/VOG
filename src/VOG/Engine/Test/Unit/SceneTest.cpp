#include "VOG/Engine/Renderer/MaterialDefinition.hpp"
#include "VOG/Engine/Renderer/MeshDefinition.hpp"
#include <VOG/Engine/Scene/Scene.hpp>

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "RenderQueueTestAccess.hpp"

using namespace VOG::Engine;
using VOG::Math::Matrix4x4f;
using VOG::Math::Vector3f;
using VOG::Math::Vector4f;

namespace VOG::Tests
{
// NOLINTBEGIN(readability-magic-numbers, readability-function-cognitive-complexity): the numbers
// are test data; the score of a test comes from the gtest assertion macros.
namespace
{
using Log = std::vector<std::string>;

const FrameContext gFrame{};

/**
 * Logs its calls as "<name>.start", "<name>.update" and "<name>.destroy", then runs its hook.
 * Declare the Log before the Scene: ~Scene() calls onDestroy().
 */
class Probe final : public Behaviour
{
public:
    Probe(Log& log, std::string name)
        : mLog{log}
        , mName{std::move(name)}
    {
    }

    std::function<void()> startHook;
    std::function<void()> updateHook;
    std::function<void()> destroyHook;

    void
    start() override
    {
        call(".start", startHook);
    }

    void
    update(const FrameContext& /*frame*/) override
    {
        call(".update", updateHook);
    }

    void
    onDestroy() override
    {
        call(".destroy", destroyHook);
    }

private:
    void
    call(const char* what, const std::function<void()>& hook)
    {
        mLog.push_back(mName + what);
        if (hook)
        {
            hook();
        }
    }

    Log&        mLog;
    std::string mName;
};

/** An object with a Probe that has started. */
GameObject
startedObject(Scene& scene, Log& log, const std::string& name, Probe** probe = nullptr)
{
    GameObject object = scene.createGameObject();
    Probe&     added  = object.addBehaviour<Probe>(log, name);
    if (probe != nullptr)
    {
        *probe = &added;
    }
    scene.update(gFrame);
    log.clear();

    return object;
}

void
expectNear(const Vector4f& actual, const Vector4f& expected)
{
    constexpr float kTolerance = 1e-5f;

    EXPECT_NEAR(actual.x, expected.x, kTolerance);
    EXPECT_NEAR(actual.y, expected.y, kTolerance);
    EXPECT_NEAR(actual.z, expected.z, kTolerance);
    EXPECT_NEAR(actual.w, expected.w, kTolerance);
}
} // namespace

TEST(SceneTest, defaultTransformIsIdentity)
{
    Scene      scene;
    Transform& transform = scene.createGameObject().getTransform();

    EXPECT_EQ(transform.position, Vector3f(0.0f));
    EXPECT_EQ(transform.rotation, glm::identity<VOG::Math::Quaternionf>());
    EXPECT_EQ(transform.scale, Vector3f(1.0f));
    EXPECT_EQ(transform.matrix(), Matrix4x4f(1.0f));
}

TEST(SceneTest, transformMatrixIsTranslateRotateScale)
{
    Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = glm::angleAxis(glm::radians(90.0f), Vector3f{0.0f, 0.0f, 1.0f});
    transform.scale    = {2.0f, 3.0f, 4.0f};

    // Scale first, then rotate, then translate.
    expectNear(transform.matrix() * Vector4f{1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 4.0f, 3.0f, 1.0f});
    expectNear(transform.matrix() * Vector4f{0.0f, 1.0f, 0.0f, 1.0f}, {-2.0f, 2.0f, 3.0f, 1.0f});
}

TEST(SceneTest, startRunsOnceBeforeFirstUpdate)
{
    Log   log;
    Scene scene;
    scene.createGameObject().addBehaviour<Probe>(log, "a");
    EXPECT_TRUE(log.empty());

    scene.update(gFrame);
    scene.update(gFrame);

    EXPECT_EQ(log, (Log{"a.start", "a.update", "a.update"}));
}

TEST(SceneTest, addBehaviourInsideStart)
{
    Log        log;
    Scene      scene;
    GameObject object                              = scene.createGameObject();
    object.addBehaviour<Probe>(log, "a").startHook = [&]
    {
        object.addBehaviour<Probe>(log, "b");
    };

    // The behaviour added by a start() starts in the next update.
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"a.start", "a.update"}));

    log.clear();
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"b.start", "a.update", "b.update"}));
}

TEST(SceneTest, behaviourAddedDuringUpdateStartsNextFrame)
{
    Log        log;
    Scene      scene;
    GameObject object                               = scene.createGameObject();
    bool       added                                = false;
    object.addBehaviour<Probe>(log, "a").updateHook = [&]
    {
        if (!std::exchange(added, true))
        {
            object.addBehaviour<Probe>(log, "b");
        }
    };

    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"a.start", "a.update"}));

    log.clear();
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"b.start", "a.update", "b.update"}));
}

TEST(SceneTest, throwingStartIsNotRetried)
{
    Log   log;
    Scene scene;
    scene.createGameObject().addBehaviour<Probe>(log, "a").startHook = []
    {
        throw std::runtime_error{"start failed"};
    };

    EXPECT_THROW(scene.update(gFrame), std::runtime_error);
    EXPECT_EQ(log, (Log{"a.start"}));

    // A start() that threw counts as started: it gets update() and is not started again.
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"a.start", "a.update"}));

    // And onDestroy().
    scene.clear();
    EXPECT_EQ(log, (Log{"a.start", "a.update", "a.destroy"}));
}

TEST(SceneTest, destroyIsDeferredToFlush)
{
    Log        log;
    Scene      scene;
    GameObject object = startedObject(scene, log, "a");

    object.destroy();
    object.destroy();
    EXPECT_TRUE(object.isValid());
    EXPECT_TRUE(log.empty());

    // The behaviour of a dying object no longer updates; the flush ends it.
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"a.destroy"}));
    EXPECT_FALSE(object.isValid());
}

TEST(SceneTest, destroyBeforeStart)
{
    Log        log;
    Scene      scene;
    GameObject object = scene.createGameObject();
    object.addBehaviour<Probe>(log, "a");

    object.destroy();
    scene.update(gFrame);

    // Never started, so no onDestroy().
    EXPECT_TRUE(log.empty());
    EXPECT_FALSE(object.isValid());
}

TEST(SceneTest, destroyFromOnDestroyFlushesSameUpdate)
{
    Log        log;
    Scene      scene;
    Probe*     probe   = nullptr;
    GameObject first   = startedObject(scene, log, "a", &probe);
    GameObject second  = startedObject(scene, log, "b");
    probe->destroyHook = [&]
    {
        second.destroy();
    };

    first.destroy();
    scene.update(gFrame);

    // b was not dying when the update pass ran; a's onDestroy() marked it for the same flush.
    EXPECT_EQ(log, (Log{"b.update", "a.destroy", "b.destroy"}));
    EXPECT_FALSE(first.isValid());
    EXPECT_FALSE(second.isValid());
}

TEST(SceneTest, addBehaviourInOnDestroyIsDropped)
{
    Log        log;
    Scene      scene;
    Probe*     probe   = nullptr;
    GameObject object  = startedObject(scene, log, "a", &probe);
    probe->destroyHook = [&]
    {
        object.addBehaviour<Probe>(log, "late");
    };

    object.destroy();
    scene.update(gFrame);
    scene.update(gFrame);

    // It neither starts nor gets onDestroy().
    EXPECT_EQ(log, (Log{"a.destroy"}));
}

TEST(SceneTest, queuedDestroyDrainsAfterThrowingUpdate)
{
    Log    log;
    Scene  scene;
    Probe* thrower = nullptr;
    startedObject(scene, log, "a", &thrower);
    GameObject victim   = startedObject(scene, log, "b");
    thrower->updateHook = []
    {
        throw std::runtime_error{"update failed"};
    };

    victim.destroy();
    EXPECT_THROW(scene.update(gFrame), std::runtime_error);

    // The flush was skipped; the object waits, dying.
    EXPECT_TRUE(victim.isValid());
    EXPECT_EQ(log, (Log{"a.update"}));

    thrower->updateHook = nullptr;
    scene.update(gFrame);
    EXPECT_FALSE(victim.isValid());
    EXPECT_EQ(log, (Log{"a.update", "a.update", "b.destroy"}));
}

TEST(SceneTest, throwingOnDestroyStillDestroysObject)
{
    Log        log;
    Scene      scene;
    Probe*     probe  = nullptr;
    GameObject object = startedObject(scene, log, "a", &probe);
    object.addBehaviour<Probe>(log, "b");
    scene.update(gFrame);
    log.clear();
    probe->destroyHook = []
    {
        throw std::runtime_error{"onDestroy failed"};
    };

    object.destroy();
    EXPECT_NO_THROW(scene.update(gFrame));

    // The failure is logged; the other behaviour and the object are not affected.
    EXPECT_EQ(log, (Log{"a.destroy", "b.destroy"}));
    EXPECT_FALSE(object.isValid());
}

TEST(SceneTest, clearContinuesAfterThrowingOnDestroy)
{
    Log        log;
    Scene      scene;
    Probe*     probe   = nullptr;
    GameObject first   = startedObject(scene, log, "a", &probe);
    GameObject second  = startedObject(scene, log, "b");
    probe->destroyHook = []
    {
        throw std::runtime_error{"onDestroy failed"};
    };

    EXPECT_NO_THROW(scene.clear());

    EXPECT_EQ(log, (Log{"a.destroy", "b.destroy"}));
    EXPECT_FALSE(first.isValid());
    EXPECT_FALSE(second.isValid());
}

TEST(SceneTest, nestedUpdateThrows)
{
    Log    log;
    Scene  scene;
    Probe* probe = nullptr;
    startedObject(scene, log, "a", &probe);
    bool entered      = false;
    bool rejected     = false;
    probe->updateHook = [&]
    {
        // Once: a scene that lets the nested update run would recurse without end.
        if (std::exchange(entered, true))
        {
            return;
        }
        try
        {
            scene.update(gFrame);
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
    };

    scene.update(gFrame);
    EXPECT_TRUE(rejected);

    // The busy flag is released: the scene works again.
    probe->updateHook = nullptr;
    log.clear();
    scene.update(gFrame);
    EXPECT_EQ(log, (Log{"a.update"}));
}

TEST(SceneTest, clearDuringUpdateThrows)
{
    Log        log;
    Scene      scene;
    Probe*     probe    = nullptr;
    GameObject object   = startedObject(scene, log, "a", &probe);
    bool       rejected = false;
    probe->updateHook   = [&]
    {
        try
        {
            scene.clear();
        }
        catch (const std::logic_error&)
        {
            rejected = true;
        }
    };

    scene.update(gFrame);

    EXPECT_TRUE(rejected);
    EXPECT_TRUE(object.isValid());
}

TEST(SceneTest, clearOrUpdateFromOnDestroyDuringClearIsRejected)
{
    Log        log;
    Scene      scene;
    Probe*     probe    = nullptr;
    GameObject object   = startedObject(scene, log, "a", &probe);
    int        rejected = 0;
    probe->destroyHook  = [&]
    {
        for (const auto& call : {std::function<void()>{[&]
                                                       {
                                                           scene.clear();
                                                       }},
                                 std::function<void()>{[&]
                                                       {
                                                           scene.update(gFrame);
                                                       }}})
        {
            try
            {
                call();
            }
            catch (const std::logic_error&)
            {
                ++rejected;
            }
        }
    };

    scene.clear();

    EXPECT_EQ(rejected, 2);
    EXPECT_FALSE(object.isValid());
    EXPECT_EQ(log, (Log{"a.destroy"}));
}

TEST(SceneTest, updateOrClearInsideEachThrows)
{
    Log        log;
    Scene      scene;
    GameObject object = startedObject(scene, log, "a");
    object.destroy();

    int visited = 0;
    scene.each<Transform>(
        [&](GameObject, Transform&)
        {
            ++visited;
            EXPECT_THROW(scene.update(gFrame), std::logic_error);
            EXPECT_THROW(scene.clear(), std::logic_error);
            EXPECT_THROW(scene.createGameObject(), std::logic_error);
        });

    // All three threw before they changed anything: the queued destroy still drains.
    EXPECT_EQ(visited, 1);
    EXPECT_TRUE(object.isValid());
    EXPECT_TRUE(log.empty());
    scene.update(gFrame);
    EXPECT_FALSE(object.isValid());
    EXPECT_EQ(log, (Log{"a.destroy"}));
}

TEST(SceneTest, destroyInsideEachIsDeferred)
{
    // Nothing was destroyed yet, so the pool of the destroy marker is created inside each().
    Scene      scene;
    GameObject first  = scene.createGameObject();
    GameObject second = scene.createGameObject();

    int visited = 0;
    scene.each<Transform>(
        [&](GameObject object, Transform&)
        {
            ++visited;
            object.destroy();
            EXPECT_TRUE(object.isValid());
        });

    EXPECT_EQ(visited, 2);
    EXPECT_TRUE(first.isValid());
    EXPECT_TRUE(second.isValid());
    scene.update(gFrame);
    EXPECT_FALSE(first.isValid());
    EXPECT_FALSE(second.isValid());
}

TEST(SceneTest, deadObjectOperations)
{
    Log        log;
    Scene      scene;
    GameObject flushed = scene.createGameObject();
    flushed.destroy();
    scene.update(gFrame);
    GameObject cleared = scene.createGameObject();
    scene.clear();

    GameObject neverCreated;
    for (GameObject* object : {&flushed, &cleared, &neverCreated})
    {
        EXPECT_FALSE(object->isValid());
        EXPECT_EQ(object->getComponent<MeshRenderer>(), nullptr);
        EXPECT_EQ(object->getComponent<Transform>(), nullptr);
        EXPECT_NO_THROW(object->removeComponent<MeshRenderer>());
        EXPECT_NO_THROW(object->destroy());
        EXPECT_THROW(object->getTransform(), std::logic_error);
        EXPECT_THROW(object->addComponent<MeshRenderer>(), std::logic_error);
        EXPECT_THROW(object->addBehaviour<Probe>(log, "a"), std::logic_error);
    }

    EXPECT_EQ(&flushed.getScene(), &scene);
    EXPECT_THROW(neverCreated.getScene(), std::logic_error);

    // A new object in the slot of a dead one is not the dead one.
    GameObject fresh = scene.createGameObject();
    EXPECT_TRUE(fresh.isValid());
    EXPECT_FALSE(flushed.isValid());
    EXPECT_NE(fresh, flushed);
}

TEST(SceneTest, secondCameraThrowsAtAddComponent)
{
    Scene      scene;
    GameObject first  = scene.createGameObject();
    GameObject second = scene.createGameObject();
    first.addComponent<Camera>();

    EXPECT_THROW(second.addComponent<Camera>(), std::logic_error);
    EXPECT_EQ(second.getComponent<Camera>(), nullptr);

    // The old camera has to go first.
    first.removeComponent<Camera>();
    EXPECT_NO_THROW(second.addComponent<Camera>());
}

TEST(SceneTest, meshRendererSubmitsTransformMatrix)
{
    Scene scene;
    auto  mesh     = std::make_shared<const Mesh>();
    auto  material = std::make_shared<const Material>();

    GameObject object              = scene.createGameObject();
    object.getTransform().position = {1.0f, 2.0f, 3.0f};
    object.getTransform().scale    = {2.0f, 2.0f, 2.0f};
    object.addComponent<MeshRenderer>(MeshRenderer{.mesh = mesh, .material = material});

    RenderQueue queue;
    scene.onDraw(gFrame, queue);

    const auto& draws = RenderQueueTestAccess::draws(queue);
    ASSERT_EQ(draws.size(), 1u);
    EXPECT_EQ(draws[0].mesh, mesh);
    EXPECT_EQ(draws[0].material, material);
    EXPECT_EQ(draws[0].world, object.getTransform().matrix());
}

TEST(SceneTest, objectWithoutMeshRendererDrawsNothing)
{
    Scene scene;
    scene.createGameObject();
    scene.createGameObject().addComponent<Camera>();

    RenderQueue queue;
    scene.onDraw(gFrame, queue);

    EXPECT_TRUE(RenderQueueTestAccess::draws(queue).empty());
}

TEST(SceneTest, cameraComponentSetsFrameView)
{
    constexpr float kTolerance = 1e-5f;

    Scene      scene;
    GameObject object              = scene.createGameObject();
    object.getTransform().position = {3.0f, 4.0f, 5.0f};
    object.getTransform().rotation =
        glm::angleAxis(glm::radians(90.0f), Vector3f{0.0f, 1.0f, 0.0f});
    object.addComponent<Camera>(Camera{.nearPlane = 2.0f, .farPlane = 300.0f});

    RenderQueue queue;
    scene.onDraw(gFrame, queue);

    const CameraView& view = RenderQueueTestAccess::camera(queue);
    EXPECT_EQ(view.position, (Vector3f{3.0f, 4.0f, 5.0f}));
    EXPECT_NEAR(view.direction.x, 1.0f, kTolerance);
    EXPECT_NEAR(view.direction.y, 0.0f, kTolerance);
    EXPECT_NEAR(view.direction.z, 0.0f, kTolerance);
    EXPECT_NEAR(view.up.x, 0.0f, kTolerance);
    EXPECT_NEAR(view.up.y, 1.0f, kTolerance);
    EXPECT_NEAR(view.up.z, 0.0f, kTolerance);
    EXPECT_EQ(view.nearPlane, 2.0f);
    EXPECT_EQ(view.farPlane, 300.0f);
}

TEST(SceneTest, noCameraLeavesDefaultView)
{
    Scene scene;
    scene.createGameObject();

    // A view the queue already has survives: the scene sets none without a Camera.
    RenderQueue queue;
    queue.setCamera(CameraView{.nearPlane = 7.0f});
    scene.onDraw(gFrame, queue);

    EXPECT_EQ(RenderQueueTestAccess::camera(queue).nearPlane, 7.0f);
}

// NOLINTEND(readability-magic-numbers, readability-function-cognitive-complexity)
} // namespace VOG::Tests
