#pragma once

#include <concepts>
#include <mutex>

namespace VOG::Common
{
template <typename T>
class ThreadsafeBoundAccessor
{
public:
    ThreadsafeBoundAccessor(T& object, std::mutex& mutex)
        : mObject{object}
        , mLock{mutex}
    {
    }

    T*
    operator->()
    {
        return std::addressof(mObject);
    }

protected:
    T&                           mObject;
    std::unique_lock<std::mutex> mLock;
};

template <typename T>
class ThreadsafeWrapper
{
public:
    template <typename... Args>
        requires(std::is_constructible_v<T, Args...>)
    ThreadsafeWrapper(Args&&... args)
        : mObject{std::forward<Args>(args)...}
    {
    }

    ThreadsafeBoundAccessor<T>
    get()
    {
        return {mObject, mMutex};
    }

protected:
    T          mObject;
    std::mutex mMutex;
};
} // namespace VOG::Common
