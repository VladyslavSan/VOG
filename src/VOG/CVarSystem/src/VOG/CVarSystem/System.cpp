#include <VOG/CVarSystem/System.hpp>

#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace VOG::CVarSystem
{
template <typename T>
class Array : public std::vector<std::unique_ptr<VariableStorage<T>>>
{
public:
    using std::vector<std::unique_ptr<VariableStorage<T>>>::vector;

    const T&
    getCurrent(std::uint32_t index)
    {
        return this->operator[](index)->current;
    };

    void
    setCurrent(const T& val, int32_t index)
    {
        this->operator[](index)->current = val;
    }

    auto
    add(const T& value, Parameter param)
    {
        this->emplace_back(new VariableStorage<T>{value, value, std::move(param)});
        return this->size();
    }
};

template <typename T>
constexpr auto
getType()
{
    using V = std::remove_cvref_t<T>;

    if constexpr (std::is_same_v<V, std::int32_t>)
    {
        return Type::eInt;
    }
    else if constexpr (std::is_same_v<V, float>)
    {
        return Type::eFloat;
    }
    else if constexpr (std::is_same_v<V, std::string>)
    {
        return Type::eString;
    }
    else
    {
        []<bool flag = false>()
        {
            static_assert(flag, "Not supported type");
        }();
    }
}

class SystemImpl final : public SystemInterface
{
    constexpr static std::size_t kInitialCapacity = 20;

    struct VariableRecord
    {
        Type        type  = Type::eInt;
        std::size_t index = std::numeric_limits<std::uint32_t>::max();
    };

public:
    template <class T>
    auto
    tryAdd(const std::string& name,
           const T&           defaultValue,
           const std::string& description,
           Flags              flags)
    {
        auto emplaceResult = mVariables.try_emplace(name, VariableRecord{.type = getType<T>()});
        const bool addedNewVariable = emplaceResult.second;

        if (!addedNewVariable && emplaceResult.first->second.type != getType<T>())
        {
            throw std::runtime_error{
                "Trying to insert variable to CVarSystem with existing name but different type."};
        }

        if constexpr (getType<T>() == Type::eInt)
        {
            if (addedNewVariable)
            {
                intVariables.add(
                    defaultValue,
                    Parameter{.name = name, .description = description, .flags = flags});

                emplaceResult.first->second.index = intVariables.size() - 1;
            }

            return std::make_pair(addedNewVariable,
                                  intVariables[emplaceResult.first->second.index].get());
        }
        else if constexpr (getType<T>() == Type::eFloat)
        {
            if (addedNewVariable)
            {
                floatVariables.add(
                    defaultValue,
                    Parameter{.name = name, .description = description, .flags = flags});

                emplaceResult.first->second.index = floatVariables.size() - 1;
            }

            return std::make_pair(addedNewVariable,
                                  floatVariables[emplaceResult.first->second.index].get());
        }
        else if constexpr (getType<T>() == Type::eString)
        {
            if (addedNewVariable)
            {
                stringVariables.add(
                    defaultValue,
                    Parameter{.name = name, .description = description, .flags = flags});

                emplaceResult.first->second.index = stringVariables.size() - 1;
            }

            return std::make_pair(addedNewVariable,
                                  stringVariables[emplaceResult.first->second.index].get());
        }
    }

    VariableStorage<int32_t>*
    registerConsoleVariable(const std::string& name,
                            std::int32_t       defaultValue,
                            const std::string& description,
                            Flags              flags = Flags::eNone) override
    {
        auto added = tryAdd(name, defaultValue, description, flags);
        if (!added.first)
        {
            // handle
        }
        return added.second;
    }

    VariableStorage<float>*
    registerConsoleVariable(const std::string& name,
                            float              defaultValue,
                            const std::string& description,
                            Flags              flags = Flags::eNone) override
    {
        auto added = tryAdd(name, defaultValue, description, flags);
        if (!added.first)
        {
            // handle
        }
        return added.second;
    }

    VariableStorage<std::string>*
    registerConsoleVariable(const std::string& name,
                            const std::string& defaultValue,
                            const std::string& description,
                            Flags              flags = Flags::eNone) override
    {
        auto added = tryAdd(name, defaultValue, description, flags);
        if (!added.first)
        {
            // handle
        }
        return added.second;
    }

protected:
    std::unordered_map<std::string, VariableRecord> mVariables;
    Array<int32_t>                                  intVariables{};
    Array<float>                                    floatVariables{};
    Array<std::string>                              stringVariables{};
};

static SystemImpl gSystem{};
SystemInterface*
SystemInterface::get()
{
    return &gSystem;
}
} // namespace VOG::CVarSystem
