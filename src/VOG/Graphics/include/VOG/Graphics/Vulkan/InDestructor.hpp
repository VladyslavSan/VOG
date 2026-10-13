#pragma once

#include <VOG/Graphics/Config/VulkanConfig.hpp>

#include <concepts>
#include <utility>

namespace VOG::Graphics::Vulkan
{
/** Logs @p error as the failure of @p what; see inDestructor(). */
void logDestructorFailure(const char* what, const vk::SystemError& error) noexcept;

/**
 * The one error policy for a Vulkan call in a destructor: a vk::SystemError (device loss, out of
 * memory) is logged and teardown continues. Nothing can recover from it there, and a logged error
 * beats an unlogged std::terminate. Other exceptions are not expected and still terminate.
 *
 * @param what  Names the call in the log line.
 */
template <std::invocable Call>
void
inDestructor(const char* what, Call&& call) noexcept // NOLINT(bugprone-exception-escape)
{
    try
    {
        std::forward<Call>(call)();
    }
    catch (const vk::SystemError& error)
    {
        logDestructorFailure(what, error);
    }
}
} // namespace VOG::Graphics::Vulkan
