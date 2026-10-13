#include "VOG/Common/Assert.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>

namespace VOG::Common::Assert
{
void
fail(const char* expression, const char* message, std::source_location location) noexcept
{
    try
    {
        spdlog::critical("Assertion failed: {} ({}) in {} at {}:{}",
                         expression,
                         message,
                         location.function_name(),
                         location.file_name(),
                         location.line());
        // Flush, not shutdown: another thread may still log before abort() ends the process.
        if (auto* logger = spdlog::default_logger_raw())
        {
            logger->flush();
        }
    }
    catch (...) // NOLINT(bugprone-empty-catch): a failing log must not hide the abort.
    {
    }

    std::abort();
}
} // namespace VOG::Common::Assert
