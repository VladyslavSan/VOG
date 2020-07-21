#include <VOG/Common/Signals.hpp>

#include <csignal>
#include <mutex>
#include <unordered_map>

namespace VOG::Common
{
using SignalT = std::pair<std::uint64_t, std::function<void()>>;
std::unordered_map<SignalType, std::vector<SignalT>> SignalMapping;
std::mutex SignalLock;
Signal::Handle NextSignalID = 0;

class SignalHandler
{
public:
    static void
    SignalGlobalHandle(int signalCode)
    {
        switch (signalCode)
        {
        case SIGABRT:
        {
            Signal::Get().HandleSignal(SignalType::Abort);
            break;
        }
        case SIGFPE:
        {
            Signal::Get().HandleSignal(SignalType::FPE);
            break;
        }
        case SIGILL:
        {
            Signal::Get().HandleSignal(SignalType::ILL);
            break;
        }
        case SIGINT:
        {
            Signal::Get().HandleSignal(SignalType::Interrupt);
            break;
        }
        case SIGSEGV:
        {
            Signal::Get().HandleSignal(SignalType::Segfault);
            break;
        }
        case SIGTERM:
        {
            Signal::Get().HandleSignal(SignalType::Terminate);
            break;
        }

        default:
            break;
        }
    }
};

Signal::Signal()
{
    std::signal(SIGABRT, SignalHandler::SignalGlobalHandle);
    std::signal(SIGFPE, SignalHandler::SignalGlobalHandle);
    std::signal(SIGILL, SignalHandler::SignalGlobalHandle);
    std::signal(SIGINT, SignalHandler::SignalGlobalHandle);
    std::signal(SIGSEGV, SignalHandler::SignalGlobalHandle);
    std::signal(SIGTERM, SignalHandler::SignalGlobalHandle);
}

void
Signal::HandleSignal(SignalType type)
{
    std::unique_lock<std::mutex> lock{SignalLock};
    auto& SignalSlot = SignalMapping[type];
    for (auto& callback : SignalSlot)
        callback.second();
}

Signal&
Signal::Get()
{
    static Signal StaticSignal;

    return StaticSignal;
}

Signal::Handle
Signal::RegisterSignal(SignalType type, std::function<void()> callback)
{
    std::unique_lock<std::mutex> lock{SignalLock};

    auto& SignalSlot = SignalMapping[type];
    SignalSlot.push_back(std::make_pair(NextSignalID, std::move(callback)));

    return NextSignalID++;
}

bool
Signal::UnregisterSignal(Handle handle)
{
    std::unique_lock<std::mutex> lock{SignalLock};

    for (auto& signalVector : SignalMapping)
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
Signal::UnregisterAllSignals()
{
    std::unique_lock<std::mutex> lock{SignalLock};

    SignalMapping.clear();
}
} // namespace VOG::Common
