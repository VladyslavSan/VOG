#pragma once

#include <cassert>
#include <concepts>

namespace VOG::Common::Assert
{
template <typename T>
    requires std::is_constructible_v<bool, T> || std::is_nothrow_convertible_v<T, bool>
bool
BoolConvert(const T& object)
{
    return static_cast<bool>(object);
}
} // namespace VOG::Common::Assert

#define VOG_ASSERT(expression) assert(VOG::Common::Assert::BoolConvert((expression)))

#define VOG_ASSERT_MSG(expression, message)                                                        \
    assert(VOG::Common::Assert::BoolConvert((expression)) && (message))

#define VOG_UNREACHABLE VOG_ASSERT(false)

#define VOG_UNREACHABLE_MSG(message) VOG_ASSERT(false, (message))
