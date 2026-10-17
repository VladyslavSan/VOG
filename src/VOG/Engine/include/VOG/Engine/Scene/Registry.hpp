#pragma once

#include <VOG/Common/Assert.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VOG::Engine::Detail
{
/**
 * A slot and the generation it had when the entity was created. Generation 0 is even, and an even
 * generation is never alive (see Registry::mGenerations), so the default is no entity.
 */
struct Entity
{
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    bool operator==(const Entity&) const = default;
};

/** The part of a component pool the registry reaches without knowing the component type. */
class PoolBase
{
public:
    virtual ~PoolBase() = default;

    /** Dooms the T of @p index: it is gone for lookups, but lives until flush(). Does nothing if
     *  absent. */
    virtual void remove(std::uint32_t index) noexcept = 0;

    /** Destroys every doomed T and frees its position. */
    virtual void flush() noexcept = 0;
};

/**
 * The components of one type, in pages of kPageSize positions. A page never moves and a T never
 * leaves its position, so its address holds from emplace() until its flush().
 *
 * A position is live, doomed or free. remove() dooms a live position: it leaves the lookups at once
 * but its T stays constructed until flush(), which frees it. emplace() fills the newest free
 * position, else a new one at the end. A doomed position is never refilled.
 *
 * Invariants, for every position p below mEnd:
 * - mSparse[i] == p exactly when the entity entry of p is i.
 * - The entity entry of p is kNone exactly when p is doomed or free.
 * - A doomed position holds a constructed T; a free one holds none.
 * Pages hold their entity entries, so that appending needs no second vector kept in step.
 */
template <class T>
class Pool final : public PoolBase
{
    static_assert(std::is_nothrow_destructible_v<T>, "flush() runs in a destructor");

public:
    static constexpr std::size_t   kPageSize = 64;
    static constexpr std::uint32_t kNone     = std::numeric_limits<std::uint32_t>::max();

    /**
     * Constructs a T for @p index, which has none, and records @p born in its position. If the
     * constructor throws, the pool is as it was. Only Registry::emplace calls it, with its own
     * index and mEpoch.
     */
    template <class... Args>
    T&
    emplace(std::uint32_t index, // NOLINT(bugprone-easily-swappable-parameters)
            std::uint64_t born,
            Args&&... args)
    {
        if (index >= mSparse.size())
        {
            mSparse.resize(std::size_t{index} + 1u, kNone);
        }

        const bool        refill   = !mFree.empty();
        const std::size_t position = refill ? mFree.back() : mEnd;
        if (!refill && position == mPages.size() * kPageSize)
        {
            addPage();
        }

        Page&             page = *mPages[position / kPageSize];
        const std::size_t slot = position % kPageSize;
        VOG_ASSERT_MSG(!page.components[slot],
                       "emplace() fills a free position, which holds no T.");
        T& component = page.components[slot].emplace(std::forward<Args>(args)...);

        // Nothing below throws: the constructor was the last step that could.
        if (refill)
        {
            mFree.pop_back();
        }
        else
        {
            ++mEnd;
        }
        page.entities[slot] = index;
        page.born[slot]     = born;
        mSparse[index]      = static_cast<std::uint32_t>(position);

        return component;
    }

    void
    remove(std::uint32_t index) noexcept override // NOLINT(bugprone-exception-escape)
    {
        if (!contains(index))
        {
            return;
        }

        // mDoomed has room for every position (see addPage), so this push does not allocate.
        VOG_ASSERT_MSG(mDoomed.size() < mDoomed.capacity(), "addPage() reserves mDoomed.");
        const std::uint32_t position = mSparse[index];
        mDoomed.push_back(position);
        pageOf(position).entities[position % kPageSize] = kNone;
        mSparse[index]                                  = kNone;
    }

    void
    flush() noexcept override // NOLINT(bugprone-exception-escape)
    {
        // mFree has room for every position as well.
        for (const std::uint32_t position : mDoomed)
        {
            VOG_ASSERT_MSG(mFree.size() < mFree.capacity(), "addPage() reserves mFree.");
            pageOf(position).components[position % kPageSize].reset();
            mFree.push_back(position);
        }
        mDoomed.clear();
    }

    bool
    contains(std::uint32_t index) const
    {
        return index < mSparse.size() && mSparse[index] != kNone;
    }

    T*
    tryGet(std::uint32_t index)
    {
        return contains(index) ? &componentAt(mSparse[index]) : nullptr;
    }

    /** Live components. */
    std::size_t
    size() const
    {
        return mEnd - mDoomed.size() - mFree.size();
    }

    /**
     * Calls visit(index, T&) for every live component whose @p born is below @p start. Registry
     * stamps what visit adds with start or more (mEpoch), so this call skips it, wherever it lands.
     * mEnd is read at each step because visit may grow it.
     */
    template <class F>
    void
    eachBornBefore(std::uint64_t start, F&& visit)
    {
        for (std::size_t position = 0; position < mEnd; ++position)
        {
            Page&               page  = pageOf(position);
            const std::size_t   slot  = position % kPageSize;
            const std::uint32_t index = page.entities[slot];
            if (index != kNone && page.born[slot] < start)
            {
                visit(index, *page.components[slot]);
            }
        }
    }

private:
    struct Page
    {
        Page() { entities.fill(kNone); }

        std::array<std::uint32_t, kPageSize>    entities;
        std::array<std::uint64_t, kPageSize>    born{};
        std::array<std::optional<T>, kPageSize> components;
    };

    Page&
    pageOf(std::size_t position) const
    {
        return *mPages[position / kPageSize];
    }

    T&
    componentAt(std::size_t position)
    {
        return *pageOf(position).components[position % kPageSize];
    }

    /**
     * Adds a page, and room in both position lists for every position the pages can hold, so that
     * remove() and flush() never allocate. Nothing changes if it throws.
     */
    void
    addPage()
    {
        auto              page     = std::make_unique<Page>();
        const std::size_t capacity = std::max<std::size_t>(2u * mEnd, mEnd + kPageSize);
        mDoomed.reserve(capacity);
        mFree.reserve(capacity);
        mPages.push_back(std::move(page));
    }

    /** Entity index to position, or kNone. */
    std::vector<std::uint32_t>         mSparse;
    std::vector<std::unique_ptr<Page>> mPages;
    std::size_t                        mEnd = 0;
    /** Positions removed since the last flush(). */
    std::vector<std::uint32_t> mDoomed;
    /** Positions below mEnd without a T, newest last. */
    std::vector<std::uint32_t> mFree;
};

/**
 * Entities and their components, one Pool per component type. Main thread only.
 *
 * Not user API: GameObject and Scene::each() are. A structural change is allowed during each(). It
 * shows at once in tryGet(), size() and isAlive(), but the T of a removed component, or of a
 * destroyed entity, is destroyed when the outermost each() returns, so a reference to it stays
 * valid until then. A component never moves. The destructor of a component must not create,
 * destroy, emplace, remove or walk: flush() iterates the doomed positions while it runs, and Debug
 * builds abort on such a call, also while the registry itself is destroyed.
 *
 * A walk visits a position only if it was filled before the walk started: emplace() stamps the
 * position with mEpoch, the number of walks started, and a walk skips a position whose stamp is not
 * older than its own start. So a walk ignores what its visitor emplaces, even in a position that an
 * earlier removal freed, and a walk that starts later sees it. The probes for the other types of
 * each() are live.
 *
 * Cost: a position stores an 8-byte stamp, and a pool reserves two lists of 4-byte positions that
 * hold at least as many entries as it has positions and at most twice as many (8 to 16 bytes per
 * position). A walk takes time in proportion to the positions its First pool ever held, with the
 * holes skipped. A pool keeps its pages until the registry dies, but a freed position is reused at
 * any depth. Once holes are refilled, the order of a walk is the order of positions, not the order
 * of emplace().
 */
class Registry final
{
public:
    /** Guards the destructors of the components against calls into the registry. */
    ~Registry() { mFlushing = true; }

    Registry()                           = default;
    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;

    Entity create();

    /** Removes @p entity and its components. Does nothing for a dead entity. */
    void destroy(Entity entity) noexcept;

    /** Destroys every alive entity. */
    void destroyAll() noexcept;

    bool isAlive(Entity entity) const noexcept;

    /**
     * Gives @p entity a T. The reference holds until T is removed from the entity or the entity
     * is destroyed, and then until the outermost running each() returns.
     * @throws std::logic_error if the entity is dead or has a T.
     */
    template <class T, class... Args>
    T&
    emplace(Entity entity, Args&&... args)
    {
        requireNotFlushing();
        if (!isAlive(entity))
        {
            throw std::logic_error{"Cannot add a component to a dead entity."};
        }
        Pool<T>& pool = findOrCreate<T>();
        if (pool.contains(entity.index))
        {
            throw std::logic_error{"The entity already has a component of this type."};
        }

        return pool.emplace(entity.index, mEpoch, std::forward<Args>(args)...);
    }

    /** Removes the T of @p entity; does nothing if it has none. */
    template <class T>
    void
    remove(Entity entity) noexcept
    {
        requireNotFlushing();
        Pool<T>* pool = find<T>();
        if (pool != nullptr && isAlive(entity))
        {
            pool->remove(entity.index);
            if (mDepth == 0)
            {
                flushPool(*pool);
            }
        }
    }

    /** The T of @p entity, or nullptr if the entity is dead or has none. */
    template <class T>
    T*
    tryGet(Entity entity)
    {
        Pool<T>* pool = find<T>();

        return pool != nullptr && isAlive(entity) ? pool->tryGet(entity.index) : nullptr;
    }

    template <class T>
    std::size_t
    size() const
    {
        const Pool<T>* pool = find<T>();

        return pool != nullptr ? pool->size() : 0u;
    }

    /**
     * Calls visitor(Entity, First&, Rest&...) for every entity that has all the types. It walks the
     * First components present when it starts, so put the rarest type first, and looks the others
     * up at each visit.
     */
    template <class First, class... Rest, class F>
    void
    each(F&& visitor)
    {
        requireNotFlushing();
        // Pools are resolved before anything runs: the map may rehash when visitor adds a pool of a
        // type that is not queried, but the pools themselves do not move.
        walk(visitor, findOrCreate<First>(), findOrCreate<Rest>()...);
    }

private:
    /** Counts the running each() calls for its lifetime; the outermost one flushes at its end. */
    class IterationScope
    {
    public:
        explicit IterationScope(Registry& registry)
            : mRegistry{registry}
        {
            ++mRegistry.mDepth;
        }

        ~IterationScope();

        IterationScope(const IterationScope&)            = delete;
        IterationScope& operator=(const IterationScope&) = delete;

    private:
        Registry& mRegistry;
    };

    /**
     * The entity of every component that @p first visits is alive: release() removes an entity from
     * every pool before it retires the generation. So the generation of the visited entity is
     * mGenerations[index].
     */
    template <class F, class First, class... Rest>
    void
    walk(F& visitor, Pool<First>& first, Pool<Rest>&... rest)
    {
        const IterationScope scope{*this};
        const std::uint64_t  start = ++mEpoch;
        first.eachBornBefore(start,
                             [&](std::uint32_t index, First& component)
                             {
                                 if ((... && rest.contains(index)))
                                 {
                                     visitor(
                                         Entity{.index = index, .generation = mGenerations[index]},
                                         component,
                                         *rest.tryGet(index)...);
                                 }
                             });
    }

    /** Removes the components of the slot and retires its generation. */
    void release(std::uint32_t index) noexcept;

    /** Destroys the doomed components of every pool. Only at depth 0, when nothing walks. */
    void flushPools() noexcept;

    /** Flushes one pool, with mFlushing set. */
    void flushPool(PoolBase& pool) noexcept;

    void
    requireNotFlushing() const noexcept
    {
        VOG_ASSERT_MSG(!mFlushing, "A component destructor must not call into the Registry.");
    }

    /** Const-shallow: the pool is owned through a unique_ptr, so a const registry yields a mutable
     *  pool. */
    template <class T>
    Pool<T>*
    find() const
    {
        const auto found = mPools.find(std::type_index{typeid(T)});

        return found != mPools.end() ? static_cast<Pool<T>*>(found->second.get()) : nullptr;
    }

    template <class T>
    Pool<T>&
    findOrCreate()
    {
        if (Pool<T>* pool = find<T>())
        {
            return *pool;
        }

        return static_cast<Pool<T>&>(
            *mPools.emplace(std::type_index{typeid(T)}, std::make_unique<Pool<T>>()).first->second);
    }

    /** Per slot; odd while an entity lives in it, so create() and destroy() each add 1. */
    std::vector<std::uint32_t> mGenerations;
    /** Retired slots. Its capacity covers every slot, so that release() does not allocate. */
    std::vector<std::uint32_t> mFreeSlots;
    /**
     * Running each() calls. The one mechanism that defers the destruction of a removed T: it is a
     * counter because each() nests, and only the outermost end flushes.
     */
    int mDepth = 0;
    /** True while flushPool() runs the destructors of doomed components. */
    bool mFlushing = false;
    /** Walks started so far. A component is stamped with it at emplace(). */
    std::uint64_t                                                  mEpoch = 0;
    std::unordered_map<std::type_index, std::unique_ptr<PoolBase>> mPools;
};
} // namespace VOG::Engine::Detail
