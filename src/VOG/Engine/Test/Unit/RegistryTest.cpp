#include <VOG/Engine/Scene/Registry.hpp>

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <set>
#include <stdexcept>
#include <vector>

using namespace VOG::Engine::Detail;

namespace VOG::Tests
{
// NOLINTBEGIN(readability-magic-numbers, readability-function-cognitive-complexity): the numbers
// only tell components apart; the score of a test comes from the gtest assertion macros.
namespace
{
struct Health
{
    int value = 0;
};

struct Armor
{
    int value = 0;
};

struct Tag
{
};

/** Counts its destructor runs, to show when a removed component dies. */
struct Counted
{
    Counted(int* destroyedCount, int initial)
        : destroyed{destroyedCount}
        , value{initial}
    {
    }

    ~Counted() { ++*destroyed; }

    Counted(const Counted&)            = delete;
    Counted& operator=(const Counted&) = delete;

    int* destroyed;
    int  value;
};

/** Removes a Health from the registry in its destructor, which a component must not do. */
struct Reentrant
{
    Registry* registry;
    Entity    other;

    ~Reentrant() { registry->remove<Health>(other); }
};

/** Throws from its constructor for a negative value. */
struct Fragile
{
    explicit Fragile(int initial)
        : value{initial}
    {
        if (initial < 0)
        {
            throw std::runtime_error{"from the constructor"};
        }
    }

    int value;
};
} // namespace

TEST(RegistryTest, staleHandleAfterDestroy)
{
    Registry     registry;
    const Entity old = registry.create();
    registry.emplace<Health>(old, 7);
    registry.destroy(old);

    // The slot is reused, its generation is not.
    const Entity fresh = registry.create();
    ASSERT_EQ(fresh.index, old.index);
    EXPECT_NE(fresh.generation, old.generation);

    EXPECT_FALSE(registry.isAlive(old));
    EXPECT_TRUE(registry.isAlive(fresh));
    EXPECT_EQ(registry.tryGet<Health>(old), nullptr);
    EXPECT_EQ(registry.tryGet<Health>(fresh), nullptr);
    EXPECT_FALSE(registry.isAlive(Entity{}));

    // A stale handle neither destroys nor removes anything of the new owner.
    registry.emplace<Health>(fresh, 9);
    registry.destroy(old);
    registry.remove<Health>(old);
    ASSERT_NE(registry.tryGet<Health>(fresh), nullptr);
    EXPECT_EQ(registry.tryGet<Health>(fresh)->value, 9);
}

TEST(RegistryTest, removeKeepsOtherComponentsIntact)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();
    registry.emplace<Health>(first, 1);
    registry.emplace<Health>(second, 2);
    registry.emplace<Health>(third, 3);
    registry.emplace<Armor>(second, 20);

    registry.remove<Health>(first);
    registry.remove<Health>(first);

    EXPECT_EQ(registry.tryGet<Health>(first), nullptr);
    EXPECT_EQ(registry.tryGet<Health>(second)->value, 2);
    EXPECT_EQ(registry.tryGet<Health>(third)->value, 3);
    EXPECT_EQ(registry.tryGet<Armor>(second)->value, 20);
    EXPECT_EQ(registry.size<Health>(), 2u);
    EXPECT_EQ(registry.size<Armor>(), 1u);
}

TEST(RegistryTest, eachVisitsOnlyEntitiesWithAllTypes)
{
    Registry     registry;
    const Entity both    = registry.create();
    const Entity onlyHp  = registry.create();
    const Entity onlyArm = registry.create();
    const Entity bothToo = registry.create();
    registry.emplace<Health>(both, 1);
    registry.emplace<Armor>(both, 10);
    registry.emplace<Health>(onlyHp, 2);
    registry.emplace<Armor>(onlyArm, 30);
    registry.emplace<Health>(bothToo, 4);
    registry.emplace<Armor>(bothToo, 40);

    std::vector<Entity> visited;
    int                 sum = 0;
    registry.each<Health, Armor>(
        [&](Entity entity, Health& health, Armor& armor)
        {
            visited.push_back(entity);
            sum += health.value + armor.value;
            ++health.value;
        });

    EXPECT_EQ(visited, (std::vector<Entity>{both, bothToo}));
    EXPECT_EQ(sum, 1 + 10 + 4 + 40);
    EXPECT_EQ(registry.tryGet<Health>(both)->value, 2);

    // A type nobody has visits nothing.
    int count = 0;
    registry.each<Health, Tag>(
        [&](Entity, Health&, Tag&)
        {
            ++count;
        });
    EXPECT_EQ(count, 0);
}

TEST(RegistryTest, destroyRemovesFromAllPools)
{
    int          destroyed = 0;
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    registry.emplace<Health>(first);
    registry.emplace<Armor>(first);
    registry.emplace<Counted>(first, &destroyed, 1);
    registry.emplace<Health>(second);
    registry.emplace<Counted>(second, &destroyed, 2);

    // Outside each() the destructors run at once.
    registry.destroy(first);
    EXPECT_EQ(registry.size<Health>(), 1u);
    EXPECT_EQ(registry.size<Armor>(), 0u);
    EXPECT_EQ(destroyed, 1);
    EXPECT_NE(registry.tryGet<Health>(second), nullptr);

    registry.destroyAll();
    EXPECT_EQ(registry.size<Health>(), 0u);
    EXPECT_EQ(destroyed, 2);
    EXPECT_FALSE(registry.isAlive(second));
}

TEST(RegistryTest, emplaceDeadOrDuplicateThrows)
{
    Registry     registry;
    const Entity entity = registry.create();
    registry.emplace<Health>(entity);

    EXPECT_THROW(registry.emplace<Health>(entity), std::logic_error);

    registry.destroy(entity);
    EXPECT_THROW(registry.emplace<Armor>(entity), std::logic_error);
    EXPECT_THROW(registry.emplace<Armor>(Entity{}), std::logic_error);
}

TEST(RegistryTest, componentAddressStableAcrossGrowth)
{
    Registry     registry;
    const Entity first   = registry.create();
    Health*      address = &registry.emplace<Health>(first, 5);

    // Growth crosses pages: the pool adds some, and none of them moves a component.
    for (std::size_t i = 0; i < 2 * Pool<Health>::kPageSize; ++i)
    {
        registry.emplace<Health>(registry.create(), static_cast<int>(i));
    }

    EXPECT_EQ(registry.tryGet<Health>(first), address);
    EXPECT_EQ(address->value, 5);
}

TEST(RegistryTest, addressStableAcrossRemoveOfOthers)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();
    Health*      gap    = &registry.emplace<Health>(first, 1);
    registry.emplace<Health>(second, 2);
    Health* last = &registry.emplace<Health>(third, 3);

    // The removal leaves a hole; nothing moves into it, and the next component fills it.
    registry.remove<Health>(first);
    EXPECT_EQ(registry.tryGet<Health>(third), last);
    Health& refilled = registry.emplace<Health>(registry.create(), 4);
    EXPECT_EQ(&refilled, gap);
    EXPECT_EQ(registry.size<Health>(), 3u);

    registry.destroy(second);
    EXPECT_EQ(registry.tryGet<Health>(third), last);
    EXPECT_EQ(last->value, 3);
}

TEST(RegistryTest, throwingConstructorLeavesPoolUnchanged)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();

    // Appending.
    EXPECT_THROW(registry.emplace<Fragile>(first, -1), std::runtime_error);
    EXPECT_EQ(registry.size<Fragile>(), 0u);
    EXPECT_EQ(registry.tryGet<Fragile>(first), nullptr);
    Fragile* address = &registry.emplace<Fragile>(first, 1);
    registry.emplace<Fragile>(second, 2);

    // Refilling: the hole is still there for the next emplace.
    registry.remove<Fragile>(first);
    EXPECT_THROW(registry.emplace<Fragile>(third, -1), std::runtime_error);
    EXPECT_EQ(registry.size<Fragile>(), 1u);
    EXPECT_EQ(registry.tryGet<Fragile>(third), nullptr);
    EXPECT_EQ(&registry.emplace<Fragile>(third, 3), address);
    EXPECT_EQ(registry.size<Fragile>(), 2u);
}

TEST(RegistryTest, removeDuringEachDefersDestructor)
{
    int          destroyed = 0;
    Registry     registry;
    const Entity entity = registry.create();
    registry.emplace<Counted>(entity, &destroyed, 7);

    int calls = 0;
    registry.each<Counted>(
        [&](Entity visited, Counted& counted)
        {
            ++calls;
            registry.remove<Counted>(visited);

            // The removal shows at once; the component lives on, so the reference stays valid.
            EXPECT_EQ(registry.tryGet<Counted>(visited), nullptr);
            EXPECT_EQ(registry.size<Counted>(), 0u);
            EXPECT_EQ(counted.value, 7);
            EXPECT_EQ(destroyed, 0);

            // Its position is not free yet: a new component of the entity lands elsewhere.
            Counted& again = registry.emplace<Counted>(visited, &destroyed, 8);
            EXPECT_NE(&again, &counted);
            EXPECT_EQ(counted.value, 7);
        });

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(destroyed, 1);
    ASSERT_NE(registry.tryGet<Counted>(entity), nullptr);
    EXPECT_EQ(registry.tryGet<Counted>(entity)->value, 8);
}

TEST(RegistryTest, nestedEachDoesNotFlush)
{
    int          destroyed = 0;
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    registry.emplace<Counted>(first, &destroyed, 1);
    registry.emplace<Counted>(second, &destroyed, 2);

    int calls = 0;
    registry.each<Counted>(
        [&](Entity, Counted& outer)
        {
            ++calls;
            registry.each<Counted>(
                [&](Entity inner, Counted&)
                {
                    registry.remove<Counted>(inner);
                });

            // Only the outermost each() flushes.
            EXPECT_EQ(destroyed, 0);
            EXPECT_EQ(outer.value, 1);
        });

    // The inner walk removed the second component before the outer walk reached it.
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(destroyed, 2);
}

TEST(RegistryTest, eachFlushesAfterThrow)
{
    int            destroyed = 0;
    Registry       registry;
    const Entity   entity  = registry.create();
    const Counted* removed = &registry.emplace<Counted>(entity, &destroyed, 1);

    const auto throwingEach = [&]
    {
        registry.each<Counted>(
            [&](Entity visited, Counted&)
            {
                registry.remove<Counted>(visited);
                throw std::runtime_error{"from the callback"};
            });
    };
    EXPECT_THROW(throwingEach(), std::runtime_error);

    // The change before the throw stays, and the scope of the walk ran its flush.
    EXPECT_EQ(destroyed, 1);
    EXPECT_EQ(registry.size<Counted>(), 0u);
    EXPECT_EQ(&registry.emplace<Counted>(entity, &destroyed, 2), removed);
}

TEST(RegistryTest, emplaceDuringEachIsNotVisitedByIt)
{
    constexpr int kOriginals = 3;
    constexpr int kSpawned   = 100;

    Registry registry;
    for (int i = 0; i < kOriginals; ++i)
    {
        registry.emplace<Health>(registry.create(), i);
    }

    int visited    = 0;
    int nestedSeen = 0;
    registry.each<Health>(
        [&](Entity, Health& health)
        {
            ++visited;
            if (health.value < kSpawned)
            {
                registry.emplace<Health>(registry.create(), kSpawned);

                // A walk that starts after the emplace sees it, and what the outer visitor
                // emplaced before.
                registry.each<Health>(
                    [&](Entity, Health&)
                    {
                        ++nestedSeen;
                    });
            }
        });

    EXPECT_EQ(visited, kOriginals);
    EXPECT_EQ(nestedSeen, (kOriginals + 1) + (kOriginals + 2) + (kOriginals + 3));
    EXPECT_EQ(registry.size<Health>(), 2u * kOriginals);

    visited = 0;
    registry.each<Health>(
        [&](Entity, Health&)
        {
            ++visited;
        });
    EXPECT_EQ(visited, 2 * kOriginals);
}

TEST(RegistryTest, emplaceDuringEachRefillsFreedPositionButIsNotVisited)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();
    registry.emplace<Health>(first, 1);
    registry.emplace<Health>(second, 2);
    const Health* freed = &registry.emplace<Health>(third, 3);
    registry.remove<Health>(third);

    int visited = 0;
    registry.each<Health>(
        [&](Entity, Health&)
        {
            if (++visited == 1)
            {
                // The hole is free, so it is refilled even though a walk runs; the walk is
                // yet to reach it and still skips it.
                Health& placed = registry.emplace<Health>(registry.create(), 100);
                EXPECT_EQ(&placed, freed);
            }
        });

    EXPECT_EQ(visited, 2);
    EXPECT_EQ(registry.size<Health>(), 3u);
}

TEST(RegistryTest, restProbesAreLive)
{
    Registry     registry;
    const Entity first = registry.create();
    const Entity gains = registry.create();
    const Entity loses = registry.create();
    registry.emplace<Health>(first);
    registry.emplace<Armor>(first);
    registry.emplace<Health>(gains);
    registry.emplace<Health>(loses);
    registry.emplace<Armor>(loses);

    std::vector<Entity> visited;
    registry.each<Health, Armor>(
        [&](Entity entity, Health&, Armor&)
        {
            visited.push_back(entity);
            if (entity == first)
            {
                registry.emplace<Armor>(gains);
                registry.remove<Armor>(loses);
            }
        });

    // Only the walked type is bounded by the start: an object ahead that gains the other type is
    // visited, one that loses it is not.
    EXPECT_EQ(visited, (std::vector<Entity>{first, gains}));
}

TEST(RegistryTest, spawnInsideEachReusesFreedPositions)
{
    constexpr int kCycles = 8;
    constexpr int kLive   = 3;

    Registry registry;
    for (int i = 0; i < kLive; ++i)
    {
        registry.emplace<Armor>(registry.create());
    }

    // Each cycle spawns Health inside an each() over Armor and destroys it outside: the peak of
    // live Health stays kLive, so the positions in use must too.
    std::set<const Health*> addresses;
    for (int cycle = 0; cycle < kCycles; ++cycle)
    {
        std::vector<Entity> spawned;
        registry.each<Armor>(
            [&](Entity, Armor&)
            {
                const Entity entity = registry.create();
                addresses.insert(&registry.emplace<Health>(entity));
                spawned.push_back(entity);
            });
        for (const Entity entity : spawned)
        {
            registry.destroy(entity);
        }
    }

    EXPECT_EQ(addresses.size(), static_cast<std::size_t>(kLive));
}

TEST(RegistryTest, destroyAndRecreateDuringEach)
{
    int          destroyed = 0;
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    registry.emplace<Counted>(first, &destroyed, 1);
    registry.emplace<Counted>(second, &destroyed, 2);

    int    calls = 0;
    Entity fresh;
    registry.each<Counted>(
        [&](Entity, Counted&)
        {
            ++calls;
            registry.destroy(first);
            registry.destroy(second);
            EXPECT_FALSE(registry.isAlive(first));
            EXPECT_FALSE(registry.isAlive(second));

            // The slot is reused at once, with a new generation.
            fresh = registry.create();
            EXPECT_EQ(fresh.index, second.index);
            EXPECT_NE(fresh.generation, second.generation);
            registry.emplace<Counted>(fresh, &destroyed, 5);
            EXPECT_EQ(destroyed, 0);
        });

    // The destroyed second entity was not visited, and neither was the new component of its
    // reused slot.
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(destroyed, 2);
    EXPECT_EQ(registry.size<Counted>(), 1u);
    ASSERT_NE(registry.tryGet<Counted>(fresh), nullptr);
    EXPECT_EQ(registry.tryGet<Counted>(fresh)->value, 5);
}

TEST(RegistryTest, destroyAllDuringEachDefersDestructors)
{
    int      destroyed = 0;
    Registry registry;
    for (int i = 0; i < 3; ++i)
    {
        registry.emplace<Counted>(registry.create(), &destroyed, i);
    }

    registry.each<Counted>(
        [&](Entity, Counted& counted)
        {
            registry.destroyAll();
            EXPECT_EQ(registry.size<Counted>(), 0u);
            EXPECT_EQ(destroyed, 0);
            EXPECT_EQ(counted.value, 0);
        });

    EXPECT_EQ(destroyed, 3);
}

TEST(RegistryTest, destroyAllSkipsDeadSlots)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();
    registry.destroy(second);
    registry.destroyAll();

    // A retired slot is not retired again: the free slots are all different and all alive.
    const Entity one = registry.create();
    const Entity two = registry.create();
    EXPECT_NE(one.index, two.index);
    EXPECT_TRUE(registry.isAlive(one));
    EXPECT_TRUE(registry.isAlive(two));
    EXPECT_FALSE(registry.isAlive(first));
    EXPECT_FALSE(registry.isAlive(third));
}

#ifndef NDEBUG
TEST(RegistryDeathTest, componentDestructorCallingIntoRegistryAborts)
{
    // The default logger writes to stdout; a death test reads stderr.
    const auto useStderr = []
    {
        spdlog::set_default_logger(spdlog::stderr_logger_mt("assert"));
    };

    EXPECT_DEATH(
        {
            useStderr();
            Registry     registry;
            const Entity entity = registry.create();
            registry.emplace<Health>(entity);
            registry.emplace<Reentrant>(entity, &registry, entity);
            registry.remove<Reentrant>(entity);
        },
        "must not call into the Registry");

    // The destructor of the registry runs the destructors of the components too.
    EXPECT_DEATH(
        {
            useStderr();
            Registry     registry;
            const Entity entity = registry.create();
            registry.emplace<Health>(entity);
            registry.emplace<Reentrant>(entity, &registry, entity);
        },
        "must not call into the Registry");
}
#endif

// NOLINTEND(readability-magic-numbers, readability-function-cognitive-complexity)
} // namespace VOG::Tests
