#include <VOG/CVarSystem/System.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>

// NOLINTBEGIN(readability-magic-numbers)
namespace
{
using VOG::CVarSystem::Variable;

TEST(CVarSystemTest, IntVariableRoundTrip)
{
    Variable<std::int32_t> variable{"test.int", 42, "An int variable"};

    EXPECT_EQ(variable.get(), 42);

    variable.set(7);
    EXPECT_EQ(variable.get(), 7);
}

TEST(CVarSystemTest, FloatVariableRoundTrip)
{
    Variable<float> variable{"test.float", 1.5f, "A float variable"};

    EXPECT_FLOAT_EQ(variable.get(), 1.5f);

    variable.set(-2.25f);
    EXPECT_FLOAT_EQ(variable.get(), -2.25f);
}

TEST(CVarSystemTest, StringVariableRoundTrip)
{
    Variable<std::string> variable{"test.string", "initial", "A string variable"};

    EXPECT_EQ(variable.get(), "initial");

    variable.set("changed");
    EXPECT_EQ(variable.get(), "changed");
}

TEST(CVarSystemTest, SameNameSharesStorage)
{
    Variable<std::int32_t> first{"test.shared", 1, "Shared variable"};
    Variable<std::int32_t> second{"test.shared", 99, "Shared variable duplicate"};

    // Registration of an existing name keeps the original value.
    EXPECT_EQ(second.get(), 1);

    first.set(10);
    EXPECT_EQ(second.get(), 10);
}

TEST(CVarSystemTest, SameNameDifferentTypeThrows)
{
    Variable<std::int32_t> existing{"test.conflict", 1, "Int variable"};

    EXPECT_THROW((Variable<float>{"test.conflict", 1.0f, "Float variable"}), std::runtime_error);
}
} // namespace

// NOLINTEND(readability-magic-numbers)
