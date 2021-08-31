#include "VOG/Common/Signals.hpp"

#include <csignal>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace VOG::Common
{
using SignalT = std::pair<std::uint64_t, std::function<void()>>;
std::unordered_map<SignalType, std::vector<SignalT>> gSignalMapping;
std::mutex                                           gSignalLock;
Signal::Handle                                       gNextSignalId   = 0;
std::once_flag                                       gInitializeOnce = {};

class SignalHandler
{
public:
    static void
    signalGlobalHandle(int signalCode)
    {
        switch (signalCode)
        {
        case SIGABRT:
        {
            Signal::handleSignal(SignalType::eAbort);
            break;
        }
        case SIGFPE:
        {
            Signal::handleSignal(SignalType::eFpe);
            break;
        }
        case SIGILL:
        {
            Signal::handleSignal(SignalType::eIll);
            break;
        }
        case SIGINT:
        {
            Signal::handleSignal(SignalType::eInterrupt);
            break;
        }
        case SIGSEGV:
        {
            Signal::handleSignal(SignalType::eSegfault);
            break;
        }
        case SIGTERM:
        {
            Signal::handleSignal(SignalType::eTerminate);
            break;
        }

        default:
            break;
        }
    }
};

void
Signal::handleSignal(SignalType type)
{
    std::unique_lock<std::mutex> lock{gSignalLock};
    auto&                        signalSlot = gSignalMapping[type];
    for (auto& callback : signalSlot)
    {
        callback.second();
    }
}

void
Signal::initializeImpl()
{
    std::signal(SIGABRT, SignalHandler::signalGlobalHandle);
    std::signal(SIGFPE, SignalHandler::signalGlobalHandle);
    std::signal(SIGILL, SignalHandler::signalGlobalHandle);
    std::signal(SIGINT, SignalHandler::signalGlobalHandle);
    std::signal(SIGSEGV, SignalHandler::signalGlobalHandle);
    std::signal(SIGTERM, SignalHandler::signalGlobalHandle);
}

void
Signal::initialize()
{
    std::call_once(gInitializeOnce, Signal::initializeImpl);
}

Signal::Handle
Signal::registerSignal(SignalType type, std::function<void()> callback)
{
    std::unique_lock<std::mutex> lock{gSignalLock};

    auto& signalSlot = gSignalMapping[type];
    signalSlot.push_back(std::make_pair(gNextSignalId, std::move(callback)));

    return gNextSignalId++;
}

bool
Signal::unregisterSignal(Handle handle)
{
    std::unique_lock<std::mutex> lock{gSignalLock};

    for (auto& signalVector : gSignalMapping)
    {
        auto elementToRemove = signalVector.second.end();
        for (auto curr = signalVector.second.begin(); curr != signalVector.second.end(); ++curr)
        {
            if (curr->first == handle)
            {
                elementToRemove = curr;
                break;
            }
        }
        if (elementToRemove != signalVector.second.end())
        {
            signalVector.second.erase(elementToRemove);
            return true;
        }
    }

    return false;
}

void
Signal::unregisterAllSignals()
{
    std::unique_lock<std::mutex> lock{gSignalLock};

    gSignalMapping.clear();
}
} // namespace VOG::Common
