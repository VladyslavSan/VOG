#include <VOG/Common/Assert.hpp>

#include <gtest/gtest.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>

namespace VOG::Common::Test
{
/** Each macro is one statement, so several fit in one scope and in an unbraced if. */
TEST(Assert, severalAssertsInOneScopeCompile)
{
    const int value = 3;

    VOG_ASSERT(value == 3);
    VOG_ASSERT(value > 0);
    VOG_ASSERT_MSG(value < 10, "value is small");
    if (value == 3)
        VOG_ASSERT(true);
    else
        VOG_ASSERT_MSG(false, "unreachable");
}

#ifndef NDEBUG
/** The failing assertion reports its message and expression, then aborts. */
TEST(AssertDeathTest, failureLogsAndAborts)
{
    EXPECT_DEATH(
        {
            // The default logger writes to stdout; a death test reads stderr.
            spdlog::set_default_logger(spdlog::stderr_logger_mt("assert"));
            VOG_ASSERT_MSG(1 + 1 == 3, "arithmetic is broken");
        },
        "1 \\+ 1 == 3 \\(arithmetic is broken\\)");
}
#endif
} // namespace VOG::Common::Test
