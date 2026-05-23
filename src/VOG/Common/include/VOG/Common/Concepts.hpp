#pragma once

#include <concepts>
#include <ranges>

namespace VOG::Common
{
template <class T, class V>
concept RangeOf = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;
template <class T, class V>
concept ContiguousSizedRangeOf = std::ranges::contiguous_range<T> && std::ranges::sized_range<T> &&
                                 std::is_same_v<V, std::ranges::range_value_t<T>>;
} // namespace VOG::Common