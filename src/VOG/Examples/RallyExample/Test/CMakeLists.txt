find_package(GTest REQUIRED)

add_executable(VOG.Rally.Unit RallyTest.cpp)
set_target_properties(VOG.Rally.Unit PROPERTIES FOLDER "VOG/Tests")
target_link_libraries(VOG.Rally.Unit
    PRIVATE
        RallyGame
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(VOG.Rally.Unit PROPERTIES TIMEOUT 60)
