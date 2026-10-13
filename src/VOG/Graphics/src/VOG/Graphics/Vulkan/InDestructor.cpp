#include "VOG/Graphics/Vulkan/InDestructor.hpp"

#include <spdlog/spdlog.h>

namespace VOG::Graphics::Vulkan
{
void
logDestructorFailure(const char* what, const vk::SystemError& error) noexcept
{
    try
    {
        spdlog::error("{} failed in a destructor: {}", what, error.what());
    }
    catch (...) // NOLINT(bugprone-empty-catch): a failed log line must not end a teardown.
    {
    }
}
} // namespace VOG::Graphics::Vulkan
