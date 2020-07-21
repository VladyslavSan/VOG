#include <VOG/Common/JSONContainer.hpp>

namespace VOG::Common
{
std::string
JSONContainer::GetCurrentTypeName() const
{
    // Holds no value
    if (m_valueHolder.index() == std::variant_npos)
    {
        return "InvalidType";
    }

    return m_typeInfo.string;
}

JSONContainer::operator std::string() const
{
    if (m_valueHolder.index() == std::variant_npos)
        return "None";

    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T>)
                return std::to_string(value);
            else if constexpr (std::is_same_v<std::string, T>)
                return value;
            else if constexpr (std::is_same_v<ArrayType, T>)
                return "array";
            else if constexpr (std::is_same_v<ObjectType, T>)
                return "Object";
            return "None";
        },
        m_valueHolder);
}

const JSONContainer* JSONContainer::operator[](std::string_view key) const
{
    if (!std::holds_alternative<ObjectType>(m_valueHolder))
        return nullptr;

    const ObjectType& objectRef = std::get<ObjectType>(m_valueHolder);
    auto found =
        std::find_if(objectRef.begin(), objectRef.end(),
                     [key](const auto& keyValue) -> bool { return key == keyValue.first; });
    if (found == objectRef.end())
        return nullptr;

    return std::addressof(found->second);
}

const JSONContainer* JSONContainer::operator[](std::size_t index) const
{
    if (!std::holds_alternative<ArrayType>(m_valueHolder))
        return nullptr;

    const ArrayType& objectRef = std::get<ArrayType>(m_valueHolder);
    if (objectRef.size() >= index)
        return nullptr;

    return std::addressof(objectRef[index]);
}
} // namespace VOG::Common
