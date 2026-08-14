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

TEST(JSONContainer, lifetime_array)
{
    JSONContainer container{1, -2, 3.f, "4", {5, 6, 7}};
    ASSERT_TRUE(container.isArray());

    const auto& arr = container.getArray();
    ASSERT_EQ(arr.size(), 5);
}

TEST(JSONContainer, lifetime_object)
{
    JSONContainer container{{"int", 10}};
    ASSERT_TRUE(container.isObject());

    ASSERT_EQ(container["int"].getOr<JSONContainer::SignedInt>(42), 10);
}

TEST(JSONContainer, lifetime_dictionary_complex)
{
    JSONContainer container{{"int", 10},
                            {"uint", 10u},
                            {"string", "string"},
                            {"float", 10.f},
                            {"array_of_string", {"string1", "string2", "string3"}},
                            {"object", {{"string1", "string1"}, {"string2", "string2"}}}};
    ASSERT_TRUE(container.isObject());

    // Check correct keys and the values.
    ASSERT_EQ(container["int"].getOr<int>(20), 10);
    ASSERT_EQ(container["uint"].getOr<unsigned>(20), 10u);
    ASSERT_EQ(container["string"].getOr<std::string>("nostring"), "string");

    // Check incorrect keys and that the provided value is returned.
    ASSERT_EQ(container["int10"].getOr<int>(20), 20);
    ASSERT_EQ(container["uint10"].getOr<unsigned>(20u), 20u);
    ASSERT_EQ(container["string10"].getOr<std::string>("nostring"), "nostring");

    ASSERT_TRUE(container["array_of_string"].isArray());
    ASSERT_TRUE(container["object"].isObject());
}

TEST(JSONContainer, array_index_access)
{
    JSONContainer container{10, 20, 30};
    ASSERT_TRUE(container.isArray());

    ASSERT_EQ(container[0].getOr<JSONContainer::SignedInt>(-1), 10);
    ASSERT_EQ(container[1].getOr<JSONContainer::SignedInt>(-1), 20);
    ASSERT_EQ(container[2].getOr<JSONContainer::SignedInt>(-1), 30);

    // Out of range returns the empty sentinel.
    ASSERT_EQ(container[3].getOr<JSONContainer::SignedInt>(-1), -1);
    ASSERT_EQ(container[100].getOr<JSONContainer::SignedInt>(-1), -1);
}
} // namespace VOG::Common::Test
