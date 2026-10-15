#include <VOG/Engine/Scene/Registry.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace VOG::Engine::Detail
{
/** Test-only window into the registry, befriended by Registry. */
struct RegistryTestAccess
{
    template <class T>
    static std::size_t
    componentCapacity(Registry& registry)
    {
        return registry.find<T>()->capacity();
    }
};
} // namespace VOG::Engine::Detail

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
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    registry.emplace<Health>(first);
    registry.emplace<Armor>(first);
    registry.emplace<Health>(second);

    registry.destroy(first);
    EXPECT_EQ(registry.size<Health>(), 1u);
    EXPECT_EQ(registry.size<Armor>(), 0u);
    EXPECT_NE(registry.tryGet<Health>(second), nullptr);

    registry.destroyAll();
    EXPECT_EQ(registry.size<Health>(), 0u);
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
    const Entity first    = registry.create();
    Health*      address  = &registry.emplace<Health>(first, 5);
    const auto   capacity = RegistryTestAccess::componentCapacity<Health>(registry);

    for (int i = 0; i < 1000; ++i)
    {
        registry.emplace<Health>(registry.create(), i);
    }

    // The pool's arrays moved; the component did not.
    EXPECT_NE(RegistryTestAccess::componentCapacity<Health>(registry), capacity);
    EXPECT_EQ(registry.tryGet<Health>(first), address);
    EXPECT_EQ(address->value, 5);
}

TEST(RegistryTest, addressStableAcrossRemoveOfOthers)
{
    Registry     registry;
    const Entity first  = registry.create();
    const Entity second = registry.create();
    const Entity third  = registry.create();
    registry.emplace<Health>(first, 1);
    registry.emplace<Health>(second, 2);
    Health* last = &registry.emplace<Health>(third, 3);

    // The last component fills the gap of a removed one in the dense array.
    registry.remove<Health>(first);
    EXPECT_EQ(registry.tryGet<Health>(third), last);
    registry.destroy(second);
    EXPECT_EQ(registry.tryGet<Health>(third), last);
    EXPECT_EQ(last->value, 3);
}

TEST(RegistryTest, structuralChangeDuringEachThrows)
{
    Registry     registry;
    const Entity visited = registry.create();
    const Entity other   = registry.create();
    registry.emplace<Health>(visited);
    registry.emplace<Armor>(visited);

    int calls = 0;
    registry.each<Health, Armor>(
        [&](Entity, Health&, Armor&)
        {
            ++calls;
            EXPECT_TRUE(registry.isIterating());
            // Both the first and the other queried type are locked.
            EXPECT_THROW(registry.emplace<Health>(other), std::logic_error);
            EXPECT_THROW(registry.emplace<Armor>(other), std::logic_error);
            EXPECT_THROW(registry.remove<Health>(visited), std::logic_error);
            EXPECT_THROW(registry.remove<Armor>(other), std::logic_error);
            EXPECT_THROW(registry.destroy(visited), std::logic_error);
            EXPECT_THROW(registry.destroyAll(), std::logic_error);

            // A type the each() does not query may change, even when its pool is new.
            EXPECT_NO_THROW(registry.emplace<Tag>(visited));
            EXPECT_NO_THROW(registry.remove<Tag>(visited));
            EXPECT_NO_THROW(registry.create());
        });

    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(registry.isIterating());
    EXPECT_TRUE(registry.isAlive(visited));
}

TEST(RegistryTest, eachGuardReleasedAfterThrow)
{
    Registry     registry;
    const Entity entity = registry.create();
    registry.emplace<Health>(entity);
    registry.emplace<Armor>(entity);

    const auto throwingEach = [&registry]
    {
        registry.each<Health, Armor>(
            [](Entity, Health&, Armor&)
            {
                throw std::runtime_error{"from the callback"};
            });
    };
    EXPECT_THROW(throwingEach(), std::runtime_error);

    EXPECT_FALSE(registry.isIterating());
    const Entity other = registry.create();
    EXPECT_NO_THROW(registry.emplace<Health>(other));
    EXPECT_NO_THROW(registry.emplace<Armor>(other));
    EXPECT_NO_THROW(registry.destroy(other));
}

// NOLINTEND(readability-magic-numbers, readability-function-cognitive-complexity)
} // namespace VOG::Tests
