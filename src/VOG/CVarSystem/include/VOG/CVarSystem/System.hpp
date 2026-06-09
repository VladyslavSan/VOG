#pragma once

#include <VOG/CVarSystem/Api.hpp>

#include <concepts>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace VOG::CVarSystem
{
enum class Flags : std::uint8_t
{
    eNone     = 0,
    eReadOnly = 1 << 1
};

enum class Type : std::uint8_t
{
    eInt = 0u,
    eFloat,
    eString,
};

struct Parameter
{
    const std::string name;
    const std::string description;
    const Flags       flags;
};

template <typename T>
struct VariableStorage
{
    T               initial;
    T               current;
    const Parameter parameter;
};

class CVARSYSTEM_API SystemInterface
{
public:
    static SystemInterface* get();

    virtual VariableStorage<int32_t>* registerConsoleVariable(const std::string& name,
                                                              std::int32_t       defaultValue,
                                                              const std::string& description,
                                                              Flags flags = Flags::eNone) = 0;

    virtual VariableStorage<float>* registerConsoleVariable(const std::string& name,
                                                            float              defaultValue,
                                                            const std::string& description,
                                                            Flags flags = Flags::eNone) = 0;

    virtual VariableStorage<std::string>* registerConsoleVariable(const std::string& name,
                                                                  const std::string& defaultValue,
                                                                  const std::string& description,
                                                                  Flags flags = Flags::eNone) = 0;
};

template <typename T>
    requires(std::same_as<T, std::int32_t> || std::same_as<T, float> ||
             std::same_as<T, std::string>)
class Variable
{
public:
    Variable(const std::string& name,
             T                  defaultValue,
             const std::string& description,
             Flags              flags = Flags::eNone)
        : mStorage{SystemInterface::get()->registerConsoleVariable(
              name, defaultValue, description, flags)}
    {
    }

    T
    get() const
    {
        return mStorage->current;
    }

    void
    set(T value)
    {
        mStorage->current = std::move(value);
    }

protected:
    VariableStorage<T>* mStorage;
};
} // namespace VOG::CVarSystem