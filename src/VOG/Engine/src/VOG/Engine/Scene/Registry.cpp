#include "VOG/Engine/Scene/Registry.hpp"

#include <limits>

namespace VOG::Engine::Detail
{
namespace
{
/** The last generation a slot has, odd: a living entity can have it. */
constexpr std::uint32_t gLastGeneration = std::numeric_limits<std::uint32_t>::max();
} // namespace

Entity
Registry::create()
{
    requireNotFlushing();
    if (!mFreeSlots.empty())
    {
        const std::uint32_t index = mFreeSlots.back();
        mFreeSlots.pop_back();

        return {.index = index, .generation = ++mGenerations[index]};
    }

    // Capacity first, so that nothing changes if it throws: mFreeSlots must hold every slot.
    if (mFreeSlots.capacity() <= mGenerations.size())
    {
        constexpr std::size_t kFirstCapacity = 16;
        mFreeSlots.reserve(std::max(kFirstCapacity, 2u * mGenerations.size()));
    }
    mGenerations.push_back(1u);

    return {.index = static_cast<std::uint32_t>(mGenerations.size() - 1u), .generation = 1u};
}

void
Registry::destroy(Entity entity) noexcept
{
    requireNotFlushing();
    if (isAlive(entity))
    {
        release(entity.index);
        if (mDepth == 0)
        {
            flushPools();
        }
    }
}

void
Registry::destroyAll() noexcept
{
    requireNotFlushing();
    for (std::uint32_t index = 0; index < mGenerations.size(); ++index)
    {
        if ((mGenerations[index] & 1u) != 0u)
        {
            release(index);
        }
    }
    if (mDepth == 0)
    {
        flushPools();
    }
}

bool
Registry::isAlive(Entity entity) const noexcept
{
    // An even generation is a dead slot, and the parity test keeps Entity{} dead.
    return entity.index < mGenerations.size() && mGenerations[entity.index] == entity.generation &&
           (entity.generation & 1u) != 0u;
}

void
Registry::release(std::uint32_t index) noexcept // NOLINT(bugprone-exception-escape)
{
    // The push below does not allocate: create() reserved a place in mFreeSlots for every slot.
    VOG_ASSERT_MSG(mFreeSlots.size() < mFreeSlots.capacity(), "create() reserves mFreeSlots.");
    for (const auto& entry : mPools)
    {
        entry.second->remove(index);
    }
    if (mGenerations[index] == gLastGeneration)
    {
        // Retired for good: not in mFreeSlots, and even, so no handle is alive in it. Incrementing
        // would wrap to 0, and the slot would give out generation 1 again.
        --mGenerations[index];

        return;
    }
    ++mGenerations[index];
    mFreeSlots.push_back(index);
}

void
Registry::flushPools() noexcept
{
    for (const auto& entry : mPools)
    {
        flushPool(*entry.second);
    }
}

void
Registry::flushPool(PoolBase& pool) noexcept
{
    mFlushing = true;
    pool.flush();
    mFlushing = false;
}

Registry::IterationScope::~IterationScope()
{
    if (--mRegistry.mDepth == 0)
    {
        mRegistry.flushPools();
    }
}
} // namespace VOG::Engine::Detail
