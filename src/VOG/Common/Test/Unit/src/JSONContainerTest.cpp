#include <VOG/Common/JSONContainer.hpp>
#include <gtest/gtest.h>

namespace VOG::Common::Test
{
TEST(JSONContainer, lifetime_empty) { JSONContainer container = {}; }

TEST(JSONContainer, lifetime_value_int)
{
    JSONContainer container(10);
    ASSERT_TRUE(container.holdsType<JSONContainer::SignedInt>());
}

TEST(JSONContainer, lifetime_value_uint)
{
    JSONContainer container(10u);
    ASSERT_TRUE(container.holdsType<JSONContainer::UnsignedInt>());
}

TEST(JSONContainer, lifetime_value_float)
{
    JSONContainer container(10.f);
    ASSERT_TRUE(container.holdsType<JSONContainer::FloatType>());
}

TEST(JSONContainer, lifetime_value_string)
{
    JSONContainer container("10");
    ASSERT_TRUE(container.holdsType<JSONContainer::StringType>());
}

TEST(JSONContainer, lifetime_value_voidptr)
{
    void*         ptr = reinterpret_cast<void*>(10u);
    JSONContainer container(ptr);
    ASSERT_TRUE(container.holdsType<void*>());
    ASSERT_EQ(container.getOr<void*>(nullptr), ptr);
}

TEST(JSONContainer, lifetime_value_sharedvoidptr)
{
    std::shared_ptr<void> ptr;
    JSONContainer         container(ptr);
    ASSERT_TRUE(container.holdsType<std::shared_ptr<void>>());
}

TEST(JSONContainer, lifetime_dictionary)
{
    JSONContainer container{{"int", 10},
                            {"uint", 10u},
                            {"string", "string"},
                            {"float", 10.f},
                            {"array_of_string", AsArray({"string1", "string2", "string3"})},
                            {"object", {{"string", "string"}}}};
    ASSERT_TRUE(container.isDictionary());

    // Check correct keys and the values.
    ASSERT_EQ(container["int"].getOr<int>(20), 10);
    ASSERT_EQ(container["uint"].getOr<unsigned>(20), 10u);
    ASSERT_EQ(container["string"].getOr<std::string>("nostring"), "string");

    // Check incorrect keys and that the provided value is returned.
    ASSERT_EQ(container["int10"].getOr<int>(20), 20);
    ASSERT_EQ(container["uint10"].getOr<unsigned>(20u), 20u);
    ASSERT_EQ(container["string10"].getOr<std::string>("nostring"), "nostring");
}
} // namespace VOG::Common::Test
