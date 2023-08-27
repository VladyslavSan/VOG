#pragma once

#include <concepts>
#include <ranges>

namespace VOG::Common::Concepts
{
template <class T, class V>
concept RangeOf = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;
}