#pragma once

#include <VOG/Common/Assert.hpp>

#include <algorithm>
#include <concepts>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <variant>
#include <vector>

namespace VOG::Common
{
class JSONContainer;

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
    using ObjectType = std::unordered_map<KeyType, JSONContainer>;
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
        std::uint64_t numeric = std::numeric_limits<std::uint64_t>::max();
        std::string   string  = {};
    };

public:
    constexpr static std::size_t
    numPossibleTypes()
    {
        return std::variant_size_v<StorageType>;
    }

    JSONContainer() noexcept
        : mTypeInfo{.numeric = 0, .string = {}}
    {
    }

    JSONContainer(const JSONContainer& other)      = default;
    JSONContainer(JSONContainer&& other)           = default;
    JSONContainer& operator=(const JSONContainer&) = default;
    JSONContainer& operator=(JSONContainer&&)      = default;

    template <class T>
        requires(std::same_as<std::remove_cvref_t<T>, FloatType> ||
                 std::same_as<std::remove_cvref_t<T>, DoubleType> ||
                 std::same_as<std::remove_cvref_t<T>, SignedInt> ||
                 std::same_as<std::remove_cvref_t<T>, UnsignedInt> ||
                 std::same_as<std::remove_cvref_t<T>, PointerType> ||
                 std::same_as<std::remove_cvref_t<T>, UnsafePointerType> ||
                 std::same_as<std::remove_cvref_t<T>, ObjectType> ||
                 std::same_as<std::remove_cvref_t<T>, ArrayType>)
    JSONContainer(T&& value)
        : mValueHolder{std::forward<T>(value)}
        , mTypeInfo{.numeric = typeid(std::remove_cvref_t<T>).hash_code(),
                    .string  = typeid(std::remove_cvref_t<T>).name()}
    {
    }

    // Special case for signed integral type
    template <class T>
        requires(std::is_integral_v<std::remove_cvref_t<T>> &&
                 std::is_signed_v<std::remove_cvref_t<T>> &&
                 !std::same_as<std::remove_cvref_t<T>, SignedInt>)
    JSONContainer(T&& value)
        : JSONContainer(static_cast<SignedInt>(std::forward<T>(value)))
    {
    }

    // Special case for unsigned integral type
    template <class T>
        requires(std::is_integral_v<std::remove_cvref_t<T>> &&
                 std::is_unsigned_v<std::remove_cvref_t<T>> &&
                 !std::same_as<std::remove_cvref_t<T>, UnsignedInt>)
    JSONContainer(T&& value)
        : JSONContainer(static_cast<UnsignedInt>(value))
    {
    }

    // Special case for string integral type
    template <class... Args>
        requires(std::is_constructible_v<StringType, Args...>)
    JSONContainer(Args&&... args)
        : mValueHolder{std::in_place_type<StringType>, std::forward<Args>(args)...}
        , mTypeInfo{.numeric = typeid(StringType).hash_code(), .string = typeid(StringType).name()}
    {
    }

    JSONContainer(std::initializer_list<JSONContainer> list)
    {
        // Construction from 2 parameters first of which is string means we are constructing object.
        if (list.size() == 2 && list.begin()->holdsType<StringType>())
        {
            const auto& first  = list.begin();
            const auto& second = first + 1;

            *this = ObjectType{{first->getOr<StringType>(""), *second}};

            return;
        }

        const bool isObject =
            std::all_of(list.begin(),
                        list.end(),
                        [](const JSONContainer& container)
                        {
                            return container.isObject() ||
                                   (container.isArray() && container.getArray().size() == 2u &&
                                    container.getArray()[0].holdsType<StringType>());
                        });

        if (isObject)
        {
            ObjectType object{};
            std::for_each(list.begin(),
                          list.end(),
                          [&object](const JSONContainer& container)
                          {
                              if (container.isObject())
                              {
                                  const auto& containerObj = container.getObject();
                                  if (!containerObj.empty()) [[unlikely]]
                                  {
                                      const auto& valueIterator = containerObj.begin();
                                      object.emplace(*valueIterator);
                                  }
                              }
                              else
                              {
                                  const auto& containerArr = container.getArray();
                                  const auto& first        = containerArr.begin();
                                  const auto& second       = first + 1;
                                  object.emplace(
                                      std::piecewise_construct,
                                      std::forward_as_tuple(first->getOr<StringType>("")),
                                      std::forward_as_tuple(*second));
                              }
                          });

            *this = std::move(object);
        }
        else
        {
            *this = ArrayType{list.begin(), list.end()};
        }
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
    isObject() const
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

    const ArrayType&
    getArray() const
    {
        VOG_ASSERT_MSG(isArray(), "Should be array.");

        return std::get<ArrayType>(mValueHolder);
    }

    ArrayType&
    getArray()
    {
        VOG_ASSERT_MSG(isArray(), "Should be array.");

        return std::get<ArrayType>(mValueHolder);
    }

    ObjectType&
    getObject()
    {
        VOG_ASSERT_MSG(isObject(), "Should be dictionary.");

        return std::get<ObjectType>(mValueHolder);
    }

    const ObjectType&
    getObject() const
    {
        VOG_ASSERT_MSG(isObject(), "Should be dictionary.");

        return std::get<ObjectType>(mValueHolder);
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
        {
            return {};
        }

        bool           error = false;
        std::vector<T> result;
        result.reserve(array->size());
        for (const auto& value : *array)
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
        {
            return {};
        }

        return result;
    }

    const JSONContainer& operator[](const KeyType& key) const;
    const JSONContainer& operator[](std::size_t index) const;

protected:
    StorageType mValueHolder;
    TypeInfo    mTypeInfo;
};
} // namespace VOG::Common
