#pragma once

#include <boost/container/options.hpp>
#include <boost/container/static_vector.hpp>

#include <algorithm>

namespace VOG::Graphics::Vulkan
{
template <class T, std::size_t N>
using StaticVector = boost::container::static_vector<T, N>;
template <class T, std::size_t N>
using StaticVectorStrict = boost::container::static_vector<
    T,
    N,
    boost::container::static_vector_options<boost::container::throw_on_overflow<true>>::type>;

template <class T, std::size_t N>
constexpr auto
operator<=>(const StaticVector<T, N>& lhs, const StaticVector<T, N>& rhs)
{
    return std::lexicographical_compare_three_way(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}
} // namespace VOG::Graphics::Vulkan
