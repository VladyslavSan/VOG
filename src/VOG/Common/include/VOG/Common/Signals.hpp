#pragma once

#include <cstdint>
#include <functional>

namespace VOG::Common
{
enum class SignalType : std::uint8_t
{
    Abort = 0,
    FPE,
    ILL,
    Interrupt,
    Segfault,
    Terminate
};

class SignalHandler;

class Signal
{
    friend class SignalHandler;

    Signal();

    void HandleSignal(SignalType type);

public:
    using Handle = std::uint64_t;

    static Signal& Get();

    Handle RegisterSignal(SignalType type, std::function<void()> callback);

    bool UnregisterSignal(Handle handle);

    void UnregisterAllSignals();
};
} // namespace VOG::Common
