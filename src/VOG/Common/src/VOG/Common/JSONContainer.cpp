#include "VOG/Common/JSONContainer.hpp"

namespace VOG::Common
{
// NOLINTNEXTLINE(cert-err58-cpp)
const JSONContainer JSONContainer::kEmptyJSONContainer{};

std::string
JSONContainer::getCurrentTypeName() const
{
    // Holds no value
    if (mValueHolder.index() == std::variant_npos)
    {
        return "InvalidType";
    }

    return mTypeInfo.string;
}

JSONContainer::
operator std::string() const
{
    if (mValueHolder.index() == std::variant_npos)
    {
        return "None";
    }

    return std::visit(
        [](const auto& value) -> std::string
        {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T>)
            {
                return std::to_string(value);
            }
            else if constexpr (std::is_same_v<std::string, T>)
            {
                return value;
            }
            else if constexpr (std::is_same_v<ArrayType, T>)
            {
                return "array";
            }
            else if constexpr (std::is_same_v<ObjectType, T>)
            {
                return "Object";
            }
            return "None";
        },
        mValueHolder);
}

const JSONContainer&
JSONContainer::operator[](const JSONContainer::KeyType& key) const
{
    if (!std::holds_alternative<ObjectType>(mValueHolder))
    {
        return kEmptyJSONContainer;
    }

    const ObjectType& objectRef = std::get<ObjectType>(mValueHolder);
    auto              found     = objectRef.find(key);
    if (found == objectRef.end())
    {
        return kEmptyJSONContainer;
    }

    return found->second;
}

const JSONContainer&
JSONContainer::operator[](std::size_t index) const
{
    if (!std::holds_alternative<ArrayType>(mValueHolder))
    {
        return kEmptyJSONContainer;
    }

    const ArrayType& objectRef = std::get<ArrayType>(mValueHolder);
    if (index >= objectRef.size())
    {
        return kEmptyJSONContainer;
    }

    return objectRef[index];
}
} // namespace VOG::Common
