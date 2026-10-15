#include "VOG/Graphics/Vulkan/InDestructor.hpp"

#include <gtest/gtest.h>

using namespace VOG::Graphics::Vulkan;

namespace VOG::Tests
{
namespace
{
/** Its destructor calls Vulkan the way the real ones do; the call here always fails. */
struct FailsInDestructor
{
    explicit FailsInDestructor(bool& survived)
        : mSurvived{survived}
    {
    }

    ~FailsInDestructor()
    {
        inDestructor("a failing call",
                     []
                     {
                         throw vk::DeviceLostError{"device lost"};
                     });
        mSurvived = true;
    }

    bool& mSurvived;
};
} // namespace

static_assert(noexcept(inDestructor("a call",
                                    []
                                    {
                                    })));

/** A vk::SystemError is logged and teardown goes on, instead of terminating. */
TEST(InDestructor, vulkanErrorIsLoggedAndTeardownContinues)
{
    bool survived = false;

    {
        FailsInDestructor failing{survived};
    }

    EXPECT_TRUE(survived);
}

TEST(InDestructor, callRunsWhenNothingFails)
{
    int calls = 0;

    inDestructor("a working call",
                 [&calls]
                 {
                     ++calls;
                 });

    EXPECT_EQ(calls, 1);
}
} // namespace VOG::Tests
