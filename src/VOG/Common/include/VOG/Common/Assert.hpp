#pragma once

#include <source_location>
#include <type_traits>

namespace VOG::Common::Assert
{
/**
 * Logs the failed assertion as critical and aborts. Never throws, so an assertion is allowed in a
 * destructor.
 */
[[noreturn]] void
fail(const char* expression, const char* message, std::source_location location) noexcept;

/**
 * The check behind VOG_ASSERT. A function, not a statement block, so the macro is one expression:
 * it fits an unbraced if/else, several fit in one scope, and none adds to the cognitive complexity
 * of the caller. The default argument captures the location of the assertion.
 */
template <typename T>
    requires std::is_constructible_v<bool, T>
void
check(const T&             value,
      const char*          expression,
      const char*          message,
      std::source_location location = std::source_location::current())
{
    if (!static_cast<bool>(value))
    {
        fail(expression, message, location);
    }
}
} // namespace VOG::Common::Assert

// Debug builds check the expression and abort when it is false. Release builds do not evaluate it;
// sizeof keeps the names in it "used".
#ifdef NDEBUG
    #define VOG_ASSERT_MSG(expression, message) ((void)sizeof(static_cast<bool>(expression)))
#else
    #define VOG_ASSERT_MSG(expression, message)                                                    \
        VOG::Common::Assert::check((expression), #expression, (message))
#endif

#define VOG_ASSERT(expression) VOG_ASSERT_MSG(expression, "")
