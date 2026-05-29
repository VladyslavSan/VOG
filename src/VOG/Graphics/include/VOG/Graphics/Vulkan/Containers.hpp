#pragma once

#include <boost/container/options.hpp>
#include <boost/container/static_vector.hpp>

namespace VOG::Graphics::Vulkan
{
template <class T, std::size_t N>
using StaticVector = boost::container::static_vector<T, N>;
template <class T, std::size_t N>
using StaticVectorStrict = boost::container::static_vector<
    T,
    N,
    boost::container::static_vector_options<boost::container::throw_on_overflow<true>>::type>;
} // namespace VOG::Graphics::Vulkan
