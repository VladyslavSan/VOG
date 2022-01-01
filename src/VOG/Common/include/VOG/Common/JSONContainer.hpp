#pragma once

#include <VOG/Common/Assert.hpp>

#include <concepts>
#include <cstdint>
#include <memory>
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
private:
    static const JSONContainer kEmptyJSONContainer;

public:
    // Simple value holders
    using FloatType         = float;
    using DoubleType        = double;
    using SignedInt         = std::int64_t;
    using UnsignedInt       = std::uint64_t;
    using StringType        = std::string;
    using PointerType       = std::shared_ptr<void>;
    using UnsafePointerType = void*;

    // Complex holders
    using KeyType    = std::string;
    using StoredType = std::pair<KeyType, JSONContainer>;
    using ObjectType = std::vector<StoredType>;
    using ArrayType  = std::vector<JSONContainer>;

protected:
    using StorageType = std::variant<FloatType,
                                     DoubleType,
                                     SignedInt,
                                     UnsignedInt,
                                     StringType,
                                     PointerType,
                                     UnsafePointerType,
                                     ObjectType,
                                     ArrayType>;

    struct TypeInfo
    {
        const std::uint64_t numeric = std::numeric_limits<std::uint64_t>::max();
        const std::string   string  = {};
    };

public:
    constexpr static std::size_t
    numPossibleTypes()
    {
        return std::variant_size_v<StorageType>;
    }

    JSONContainer()
        : mValueHolder{}
        , mTypeInfo{.numeric = 0, .string = {}}
    {
    }

    template <class T>
        requires(std::is_constructible_v<StorageType, std::remove_cvref_t<T>>)
    JSONContainer(T&& value)
        : mValueHolder{std::forward<T>(value)}
        , mTypeInfo{.numeric = typeid(std::remove_cvref_t<T>).hash_code(),
                    .string  = typeid(std::remove_cvref_t<T>).name()}
    {
    }

    template <class T>
        requires(std::is_signed_v<std::remove_cvref_t<T>> &&
                 !std::is_constructible_v<StorageType, std::remove_cvref_t<T>>)
    JSONContainer(T&& value)
        : JSONContainer(static_cast<SignedInt>(std::forward<T>(value)))
    {
    }

    template <class T>
        requires(std::is_unsigned_v<std::remove_cvref_t<T>> &&
                 !std::is_constructible_v<StorageType, std::remove_cvref_t<T>>)
    JSONContainer(T&& value)
        : JSONContainer(static_cast<UnsignedInt>(value))
    {
    }

    JSONContainer(std::initializer_list<std::pair<std::string_view, JSONContainer>> list)
        : mValueHolder{std::in_place_type<ObjectType>, list.begin(), list.end()}
        , mTypeInfo{.numeric = typeid(ObjectType).hash_code(), .string = typeid(ObjectType).name()}
    {
    }

    template <class T>
        requires(std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
                 std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
                 std::same_as<T, StringType> || std::same_as<T, PointerType> ||
                 std::same_as<T, UnsafePointerType>)
    JSONContainer(std::vector<T> list)
        : mValueHolder{std::in_place_type<ArrayType>, list.begin(), list.end()}
        , mTypeInfo{.numeric = typeid(ArrayType).hash_code(), .string = typeid(ArrayType).name()}
    {
    }

    std::string getCurrentTypeName() const;
    explicit    operator std::string() const;

    inline bool
    isValid() const
    {
        return !mValueHolder.valueless_by_exception();
    }

    inline bool
    isDictionary() const
    {
        return std::holds_alternative<ObjectType>(mValueHolder);
    }

    inline bool
    isArray() const
    {
        return std::holds_alternative<ArrayType>(mValueHolder);
    }

    template <class T>
        requires(std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
                 std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
                 std::same_as<T, StringType> || std::same_as<T, PointerType> ||
                 std::same_as<T, UnsafePointerType> || std::same_as<T, ObjectType> ||
                 std::same_as<T, ArrayType>)
    inline bool
    holdsType() const
    {
        return isValid() && std::holds_alternative<T>(mValueHolder);
    }

    template <class T, class U>
        requires(std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
                 std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
                 std::same_as<T, StringType> || std::same_as<T, PointerType> ||
                 std::same_as<T, UnsafePointerType>)
    inline T
    getOr(U&& defaultValue) const
    {
        const T* value = std::get_if<T>(&mValueHolder);
        return value != nullptr ? *value : static_cast<T>(std::forward<U>(defaultValue));
    }

    template <class T, class U>
        requires((std::is_unsigned_v<T> && !std::same_as<T, UnsignedInt>) ||
                 (std::is_signed_v<T> && !std::same_as<T, SignedInt>))
    inline T
    getOr(U&& defaultValue) const
    {
        if constexpr (std::is_unsigned_v<T>)
        {
            return static_cast<T>(getOr<UnsignedInt>(static_cast<U>(defaultValue)));
        }
        else
        {
            return static_cast<T>(getOr<SignedInt>(static_cast<U>(defaultValue)));
        }
    }

    template <class T>
        requires(std::same_as<T, FloatType> || std::same_as<T, DoubleType> ||
                 std::same_as<T, SignedInt> || std::same_as<T, UnsignedInt> ||
                 std::same_as<T, StringType> || std::same_as<T, PointerType> ||
                 std::same_as<T, UnsafePointerType>)
    inline std::vector<T>
    getArrayOfType() const
    {
        const ArrayType* array = std::get_if<ArrayType>(&mValueHolder);
        if (array == nullptr)
            return {};

        bool           error = false;
        std::vector<T> result;
        result.reserve(array->size());
        for (auto value : *array)
        {
            const T* valuePtr = std::get_if<T>(&value.mValueHolder);
            if (valuePtr == nullptr)
            {
                error = true;
                break;
            }

            result.push_back(*valuePtr);
        }

        if (error)
            return {};

        return result;
    }

    const JSONContainer& operator[](std::string_view key) const;
    const JSONContainer& operator[](std::size_t index) const;

protected:
    StorageType mValueHolder;
    TypeInfo    mTypeInfo;
};
} // namespace VOG::Common
