#pragma once

#include <cassert>
#include <concepts>
#include <stdexcept>
#ifdef VOG_COMMON_ASSERT_AS_EXCEPTION
#include <source_location>
#endif

namespace VOG::Common::Assert
{
template <typename T>
    requires std::is_constructible_v<bool, T> || std::is_nothrow_convertible_v<T, bool> bool
BoolConvert(const T& object)
{
    return static_cast<bool>(object);
}
} // namespace VOG::Common::Assert

#ifdef VOG_COMMON_ASSERT_AS_EXCEPTION
#define VOG_ASSERT(expression)                                                                     \
    const bool condition = VOG::Common::Assert::BoolConvert((expression));                         \
    if (!condition)                                                                                \
    {                                                                                              \
        const std::source_location location = std::source_location::current();                     \
        throw std::runtime_error{"Exception in file:" + std::string(location.file_name()) +        \
                                 " function:" + std::string(location.file_name()) + "\n"};         \
    }
#else
#define VOG_ASSERT(expression)                                                                     \
    {                                                                                              \
        const bool condition = VOG::Common::Assert::BoolConvert((expression));                     \
        assert(condition);                                                                         \
    }
#endif

#ifdef VOG_COMMON_ASSERT_AS_EXCEPTION
#define VOG_ASSERT_MSG(expression, message)                                                        \
    if (!VOG::Common::Assert::BoolConvert((expression)))                                           \
    {                                                                                              \
        const std::source_location location = std::source_location::current();                     \
        throw std::runtime_error{"Error:" + std::string(message) +                                 \
                                 " in file:" + std::string(location.file_name()) +                 \
                                 " function:" + std::string(location.file_name()) + "\n"};         \
    }
#else
#define VOG_ASSERT_MSG(expression, message)                                                        \
    {                                                                                              \
        const bool condition = VOG::Common::Assert::BoolConvert((expression));                     \
        assert(condition && (message));                                                            \
    }
#endif

#define VOG_UNREACHABLE VOG_ASSERT(false)

#define VOG_UNREACHABLE_MSG(message) VOG_ASSERT(false, (message))
