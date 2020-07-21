#pragma once

#include <cassert>
#include <concepts>

namespace VOG::Common
{
template <class T>
requires std::is_constructible_v<bool, T> bool
BoolConvert(const T& object)
{
    return static_cast<bool>(object);
}
} // namespace VOG::Common

//#define VOG_ASSERT_IF_NOT(expression, message)
//    assert(VOG::Assert::BoolConvert(expression))