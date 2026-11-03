#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace VOG::Tests
{
/**
 * Sets VK_KHRONOS_VALIDATION_VALIDATE_SYNC to "true" for its lifetime, so that the validation layer
 * checks synchronization. The layer reads its settings in vkCreateInstance. The environment is
 * process-wide, so the destructor restores what the variable was: a test binary that runs later
 * fixtures does not inherit the setting. Not for use while another thread reads the environment.
 */
// NOLINTBEGIN(concurrency-mt-unsafe): no other thread runs while the tests set or reset the
// variable, so none reads the environment at the same time.
class SyncValidationSetting
{
public:
    SyncValidationSetting()
    {
        if (const char* previous = std::getenv(kVariable))
        {
            mPrevious = previous;
        }
        set("true");
    }

    ~SyncValidationSetting() { set(mPrevious ? mPrevious->c_str() : nullptr); }

    SyncValidationSetting(const SyncValidationSetting&)            = delete;
    SyncValidationSetting(SyncValidationSetting&&)                 = delete;
    SyncValidationSetting& operator=(const SyncValidationSetting&) = delete;
    SyncValidationSetting& operator=(SyncValidationSetting&&)      = delete;

private:
    static constexpr const char* kVariable = "VK_KHRONOS_VALIDATION_VALIDATE_SYNC";

    static void
    set(const char* value)
    {
#ifdef _WIN32
        _putenv_s(kVariable, value != nullptr ? value : "");
#else
        if (value != nullptr)
        {
            setenv(kVariable, value, 1);
        }
        else
        {
            unsetenv(kVariable);
        }
#endif
    }

    std::optional<std::string> mPrevious;
};

// NOLINTEND(concurrency-mt-unsafe)
} // namespace VOG::Tests
