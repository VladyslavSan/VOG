#pragma once

#include <ranges>

namespace VOG::Common
{

/**
 * @brief Constrains @p T to any range whose element type is exactly @p V.
 *
 * @tparam T Range type to constrain.
 * @tparam V Required element type.
 *
 * @par Example
 * @code
 * void process(RangeOf<int> auto const& range);
 *
 * process(std::vector<int>{1, 2, 3});   // OK
 * process(std::list<float>{1.f});       // error: element type is float, not int
 * @endcode
 */
template <class T, class V>
concept RangeOf = std::ranges::range<T> && std::is_same_v<V, std::ranges::range_value_t<T>>;

/**
 * @brief Constrains @p T to a contiguous, O(1)-sized range whose element type is exactly @p V.
 *
 * Satisfied by types such as @c std::vector, @c std::array, @c std::span, and raw arrays.
 * Suitable wherever a stable pointer + element count pair is required, e.g. when passing
 * data directly to Vulkan API calls via @c .data() and @c .size().
 *
 * @tparam T Range type to constrain.
 * @tparam V Required element type.
 *
 * @note @c std::ranges::sized_range is already implied by @c std::ranges::contiguous_range;
 *       it is listed explicitly to make the O(1)-size guarantee visible at the call site.
 *
 * @par Example
 * @code
 * void upload(ContiguousSizedRangeOf<float> auto const& data)
 * {
 *     vkCmdUpdateBuffer(cmd, buffer, 0,
 *                       std::ranges::size(data) * sizeof(float),
 *                       std::ranges::data(data));
 * }
 *
 * upload(std::vector<float>{0.f, 1.f, 2.f});  // OK
 * upload(std::array<float, 3>{});              // OK
 * upload(std::list<float>{0.f});               // error: not contiguous
 * upload(std::vector<int>{0});                 // error: element type is int, not float
 * @endcode
 */
template <class T, class V>
concept ContiguousSizedRangeOf = std::ranges::contiguous_range<T> && std::ranges::sized_range<T> &&
                                 std::is_same_v<V, std::ranges::range_value_t<T>>;
} // namespace VOG::Common