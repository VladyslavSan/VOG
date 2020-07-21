#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <typeinfo>
#include <variant>
#include <vector>

namespace VOG::Common
{
class JSONContainer;

inline std::vector<JSONContainer>
AsArray(std::vector<JSONContainer> array)
{
    return std::move(array);
}

class JSONContainer
{
public:
    // Simple value holders
    using FloatType = float;
    using DoubleType = double;
    using SignedInt = std::int64_t;
    using UnsignedInt = std::uint64_t;
    using StringType = std::string;
    using PointerType = std::shared_ptr<void>;
    using UnsafePointerType = void*;

    // Complex holders
    using KeyType = std::string;
    using StoredType = std::pair<KeyType, JSONContainer>;
    using ObjectType = std::vector<StoredType>;
    using ArrayType = std::vector<JSONContainer>;

protected:
    using StorageType = std::variant<FloatType, DoubleType, SignedInt, UnsignedInt, StringType,
                                     PointerType, UnsafePointerType, ObjectType, ArrayType>;

public:
    constexpr static std::size_t
    NumPossibleTypes()
    {
        return std::variant_size_v<StorageType>;
    }

    JSONContainer()
        : m_valueHolder{}
        , m_typeInfo{.numeric = 0, .string = {}}
    {
    }

    JSONContainer(void* pointer)
        : m_valueHolder{std::in_place_type<UnsafePointerType>, pointer}
        , m_typeInfo{.numeric = typeid(UnsafePointerType).hash_code(),
                     .string = typeid(UnsafePointerType).name()}
    {
    }

    template <class T>
    requires std::constructible_from<StorageType, std::remove_cvref_t<T>>
    JSONContainer(T&& value)
        : m_valueHolder{std::forward<T>(value)}
        , m_typeInfo{.numeric = typeid(std::remove_cvref_t<T>).hash_code(),
                     .string = typeid(std::remove_cvref_t<T>).name()}
    {
    }

    template <class T>
    requires(std::signed_integral<std::remove_cvref_t<T>> &&
             !std::same_as<std::remove_cvref_t<T>, SignedInt>) JSONContainer(T&& value)
        : JSONContainer(static_cast<SignedInt>(std::forward<T>(value)))
    {
    }

    template <class T>
    requires(std::unsigned_integral<std::remove_cvref_t<T>> &&
             !std::same_as<std::remove_cvref_t<T>, UnsignedInt>) JSONContainer(T&& value)
        : JSONContainer(static_cast<UnsignedInt>(value))
    {
    }

    JSONContainer(std::initializer_list<std::pair<std::string_view, JSONContainer>> list)
        : m_valueHolder{std::in_place_type<ObjectType>, list.begin(), list.end()}
        , m_typeInfo{.numeric = typeid(ObjectType).hash_code(), .string = typeid(ObjectType).name()}
    {
    }

    template <class T>
        requires std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
        std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> || std::same_as<T, StringType> ||
        std::same_as<T, PointerType> ||
        std::same_as<T, UnsafePointerType>
        JSONContainer(std::vector<T> list)
        : m_valueHolder{std::in_place_type<ArrayType>}
    , m_typeInfo{.numeric = typeid(ArrayType).hash_code(), .string = typeid(ArrayType).name()}
    {
        ArrayType* array = GetArray();
        array->reserve(list.size());
        for (const auto& Value : list)
        {
            array->emplace_back(std::move(Value));
        }
    }

    std::string GetCurrentTypeName() const;
    explicit operator std::string() const;

    inline bool
    IsValid() const
    {
        return this != nullptr && !m_valueHolder.valueless_by_exception();
    }

    inline bool
    IsDictionary() const
    {
        return std::holds_alternative<ObjectType>(m_valueHolder);
    }

    inline bool
    IsArray() const
    {
        return std::holds_alternative<ArrayType>(m_valueHolder);
    }

    template <class T>
        inline bool
        HoldsType() const requires std::same_as<T, FloatType> ||
        std::same_as<T, DoubleType> || std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
        std::same_as<T, StringType> || std::same_as<T, PointerType> ||
        std::same_as<T, UnsafePointerType> || std::same_as<T, ObjectType> ||
        std::same_as<T, ArrayType>
    {
        return IsValid() && std::holds_alternative<T>(m_valueHolder);
    }

    inline ObjectType*
    GetDictionary()
    {
        return std::get_if<ObjectType>(&m_valueHolder);
    }
    inline const ObjectType*
    GetDictionary() const
    {
        return std::get_if<ObjectType>(&m_valueHolder);
    }

    inline ArrayType*
    GetArray()
    {
        return std::get_if<ArrayType>(&m_valueHolder);
    }
    inline const ArrayType*
    GetArray() const
    {
        return std::get_if<ArrayType>(&m_valueHolder);
    }

    template <class T>
        inline const T*
        GetValue() const requires std::same_as<T, FloatType> ||
        std::same_as<T, DoubleType> || std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
        std::same_as<T, StringType> || std::same_as<T, PointerType> ||
        std::same_as<T, UnsafePointerType>
    {
        return std::get_if<T>(&m_valueHolder);
    }

    template <class T>
        inline const T&
        GetValueOr(const T& alternative) const requires std::same_as<T, FloatType> ||
        std::same_as<T, DoubleType> || std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
        std::same_as<T, StringType> || std::same_as<T, PointerType> ||
        std::same_as<T, UnsafePointerType>
    {
        if (!HoldsType<T>())
            return alternative;

        return *GetValue<T>();
    }

    template <class T>
        inline std::vector<T>
        GetArrayOfType() const requires std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
        std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> || std::same_as<T, StringType> ||
        std::same_as<T, PointerType> || std::same_as<T, UnsafePointerType>
    {
        if (!HoldsType<ArrayType>())
            return {};

        bool error = false;
        const ArrayType* array = GetArray();
        std::vector<T> result;
        result.reserve(array->size());
        for (auto value : *array)
        {
            if (!value.HoldsType<T>())
            {
                error = true;
                break;
            }

            result.push_back(*value.GetValue<T>());
        }

        if (error)
            return {};

        return result;
    }

    const JSONContainer* operator[](std::string_view key) const;
    const JSONContainer* operator[](std::size_t index) const;

protected:
    StorageType m_valueHolder;

    struct TypeInfo
    {
        std::uint64_t numeric = std::numeric_limits<std::uint64_t>::max();
        std::string string = {};
    } m_typeInfo;
};
} // namespace VOG::Common
