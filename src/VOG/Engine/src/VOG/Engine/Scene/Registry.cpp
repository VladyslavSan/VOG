#include "VOG/Engine/Scene/Registry.hpp"

namespace VOG::Engine::Detail
{
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
    // An even generation is a retired slot. The parity test keeps Entity{} dead after a wraparound.
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
