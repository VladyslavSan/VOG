#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace VOG::Engine::Detail
{
/** A slot and the generation it had when the entity was created; the default is no entity. */
struct Entity
{
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;

    bool operator==(const Entity&) const = default;
};

/** Marks an entity whose GameObject::destroy() ran and whose flush has not. Never queried. */
struct PendingDestroy
{
};

/** The part of a component pool the registry reaches without knowing the component type. */
class PoolBase
{
public:
    virtual ~PoolBase() = default;

    virtual void remove(std::uint32_t index)         = 0;
    virtual bool contains(std::uint32_t index) const = 0;

    bool
    locked() const
    {
        return mLocks != 0;
    }

    void
    lock()
    {
        ++mLocks;
    }

    void
    unlock()
    {
        --mLocks;
    }

private:
    /** How many Registry::each() calls are running over this pool. */
    int mLocks = 0;
};

/**
 * The components of one type, in a dense array. A component is its own allocation and only the
 * pointers move on removal, so a T never moves: its address holds until it is removed.
 */
template <class T>
class Pool final : public PoolBase
{
public:
    template <class... Args>
    T&
    emplace(std::uint32_t index, Args&&... args)
    {
        // ponytail: one allocation per component; paged storage if iteration shows in profiles.
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        if (index >= mSparse.size())
        {
            mSparse.resize(std::size_t{index} + 1u, kNone);
        }

        // The vectors stay in step: a throw from the second push undoes the first.
        mComponents.push_back(std::move(component));
        try
        {
            mDense.push_back(index);
        }
        catch (...)
        {
            mComponents.pop_back();
            throw;
        }
        mSparse[index] = static_cast<std::uint32_t>(mDense.size() - 1u);

        return *mComponents.back();
    }

    /** Swaps the last component into the gap. */
    void
    remove(std::uint32_t index) override
    {
        const std::uint32_t position = mSparse[index];
        const std::uint32_t last     = static_cast<std::uint32_t>(mDense.size()) - 1u;

        // Outlives the bookkeeping: a component destructor runs after the pool is consistent.
        std::unique_ptr<T> removed = std::move(mComponents[position]);
        if (position != last)
        {
            mDense[position]          = mDense[last];
            mComponents[position]     = std::move(mComponents[last]);
            mSparse[mDense[position]] = position;
        }
        mDense.pop_back();
        mComponents.pop_back();
        mSparse[index] = kNone;
    }

    bool
    contains(std::uint32_t index) const override
    {
        return index < mSparse.size() && mSparse[index] != kNone;
    }

    T*
    tryGet(std::uint32_t index)
    {
        return contains(index) ? mComponents[mSparse[index]].get() : nullptr;
    }

    std::size_t
    size() const
    {
        return mDense.size();
    }

    /** The entity index of the component at @p position. */
    std::uint32_t
    entityAt(std::size_t position) const
    {
        return mDense[position];
    }

    T&
    componentAt(std::size_t position)
    {
        return *mComponents[position];
    }

    std::size_t
    capacity() const
    {
        return mComponents.capacity();
    }

private:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    /** Entity index to position in mDense, or kNone. */
    std::vector<std::uint32_t>      mSparse;
    std::vector<std::uint32_t>      mDense;
    std::vector<std::unique_ptr<T>> mComponents;
};

/**
 * Entities and their components, one Pool per component type. Main thread only.
 *
 * Not user API: GameObject and Scene::each() are. A structural change (emplace, remove, destroy)
 * that would reorder a pool a running each() visits throws std::logic_error instead: each() locks
 * every pool it queries, and destroy() and destroyAll() throw during any each().
 */
class Registry final
{
public:
    Entity
    create()
    {
        if (!mFree.empty())
        {
            const std::uint32_t index = mFree.back();
            mFree.pop_back();
            mSlots[index].alive = true;

            return {.index = index, .generation = mSlots[index].generation};
        }

        mSlots.push_back({.generation = 1u, .alive = true});

        return {.index = static_cast<std::uint32_t>(mSlots.size() - 1u), .generation = 1u};
    }

    /** Removes @p entity and its components. Does nothing for a dead entity. Throws during each().
     */
    void
    destroy(Entity entity)
    {
        requireNotIterating("destroy");
        if (isAlive(entity))
        {
            release(entity.index);
        }
    }

    /** Destroys every alive entity. Throws during each(). */
    void
    destroyAll()
    {
        requireNotIterating("destroyAll");
        for (std::uint32_t index = 0; index < mSlots.size(); ++index)
        {
            if (mSlots[index].alive)
            {
                release(index);
            }
        }
    }

    bool
    isAlive(Entity entity) const
    {
        return entity.index < mSlots.size() && mSlots[entity.index].alive &&
               mSlots[entity.index].generation == entity.generation;
    }

    /** True while an each() runs. */
    bool
    isIterating() const
    {
        return mDepth != 0;
    }

    /** True while an each() queries T: emplace and remove of T throw. */
    template <class T>
    bool
    isLocked() const
    {
        const Pool<T>* pool = find<T>();

        return pool != nullptr && pool->locked();
    }

    /**
     * Gives @p entity a T. The reference holds until T is removed from the entity or the entity
     * is destroyed.
     * @throws std::logic_error if the entity is dead, has a T or T is queried by a running each().
     */
    template <class T, class... Args>
    T&
    emplace(Entity entity, Args&&... args)
    {
        if (!isAlive(entity))
        {
            throw std::logic_error{"Cannot add a component to a dead entity."};
        }
        Pool<T>& pool = findOrCreate<T>();
        if (pool.locked())
        {
            throw std::logic_error{"Cannot add a component of a type a running each() queries."};
        }
        if (pool.contains(entity.index))
        {
            throw std::logic_error{"The entity already has a component of this type."};
        }

        return pool.emplace(entity.index, std::forward<Args>(args)...);
    }

    /**
     * Removes the T of @p entity; does nothing if it has none.
     * @throws std::logic_error if T is queried by a running each().
     */
    template <class T>
    void
    remove(Entity entity)
    {
        Pool<T>* pool = find<T>();
        if (pool == nullptr)
        {
            return;
        }
        if (pool->locked())
        {
            throw std::logic_error{"Cannot remove a component of a type a running each() queries."};
        }
        if (isAlive(entity) && pool->contains(entity.index))
        {
            pool->remove(entity.index);
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
     * pool of First and probes the others: put the rarest type first.
     */
    template <class First, class... Rest, class F>
    void
    each(F&& visitor)
    {
        // Pools are resolved before anything runs: the map may rehash when visitor adds a pool of a
        // type that is not queried, but the pools themselves do not move.
        Pool<First>&                                      first = findOrCreate<First>();
        const std::tuple<Pool<Rest>*...>                  others{&findOrCreate<Rest>()...};
        const std::array<PoolBase*, 1u + sizeof...(Rest)> queried = std::apply(
            [&first](Pool<Rest>*... pool)
            {
                return std::array<PoolBase*, 1u + sizeof...(Rest)>{&first, pool...};
            },
            others);
        const IterationGuard guard{*this, queried};

        // ponytail: iterates First's pool; caller puts the rarest type first.
        for (std::size_t position = 0; position < first.size(); ++position)
        {
            const std::uint32_t        index = first.entityAt(position);
            const std::tuple<Rest*...> found = std::apply(
                [index](Pool<Rest>*... pool)
                {
                    return std::tuple<Rest*...>{pool->tryGet(index)...};
                },
                others);
            std::apply(
                [&](Rest*... rest)
                {
                    if ((... && (rest != nullptr)))
                    {
                        visitor(Entity{.index = index, .generation = mSlots[index].generation},
                                first.componentAt(position),
                                *rest...);
                    }
                },
                found);
        }
    }

private:
    friend struct RegistryTestAccess;

    struct Slot
    {
        std::uint32_t generation = 1u;
        bool          alive      = false;
    };

    /** Locks the queried pools and counts the running each() for its lifetime. */
    template <std::size_t N>
    class IterationGuard
    {
    public:
        IterationGuard(Registry& registry, const std::array<PoolBase*, N>& pools)
            : mRegistry{registry}
            , mPools{pools}
        {
            ++mRegistry.mDepth;
            for (PoolBase* pool : mPools)
            {
                pool->lock();
            }
        }

        ~IterationGuard()
        {
            for (PoolBase* pool : mPools)
            {
                pool->unlock();
            }
            --mRegistry.mDepth;
        }

        IterationGuard(const IterationGuard&)            = delete;
        IterationGuard& operator=(const IterationGuard&) = delete;

    private:
        Registry&                mRegistry;
        std::array<PoolBase*, N> mPools;
    };

    void
    requireNotIterating(const char* what) const
    {
        if (isIterating())
        {
            throw std::logic_error{std::string{"Cannot "} + what + " while each() runs."};
        }
    }

    /** Removes the components of the slot and retires its generation. */
    void
    release(std::uint32_t index)
    {
        for (const auto& entry : mPools)
        {
            if (entry.second->contains(index))
            {
                entry.second->remove(index);
            }
        }
        ++mSlots[index].generation;
        mSlots[index].alive = false;
        mFree.push_back(index);
    }

    template <class T>
    const Pool<T>*
    find() const
    {
        const auto found = mPools.find(std::type_index{typeid(T)});

        return found != mPools.end() ? static_cast<const Pool<T>*>(found->second.get()) : nullptr;
    }

    template <class T>
    Pool<T>*
    find()
    {
        return const_cast<Pool<T>*>(std::as_const(*this).template find<T>());
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

    std::vector<Slot>          mSlots;
    std::vector<std::uint32_t> mFree;
    /** Running each() calls; plain int, the registry belongs to the main thread. */
    int                                                            mDepth = 0;
    std::unordered_map<std::type_index, std::unique_ptr<PoolBase>> mPools;
};
} // namespace VOG::Engine::Detail
