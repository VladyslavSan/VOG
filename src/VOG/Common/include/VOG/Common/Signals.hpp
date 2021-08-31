#pragma once

#include <cstdint>
#include <functional>

namespace VOG::Common
{
enum class SignalType : std::uint8_t
{
    eAbort = 0,
    eFpe,
    eIll,
    eInterrupt,
    eSegfault,
    eTerminate
};

class SignalHandler;

class Signal
{
    friend class SignalHandler;

    static void handleSignal(SignalType type);

    static void initializeImpl();

public:
    using Handle = std::uint64_t;

    static void initialize();

    static Handle registerSignal(SignalType type, std::function<void()> callback);

    static bool unregisterSignal(Handle handle);

    static void unregisterAllSignals();
};
} // namespace VOG::Common
