find_package(GTest REQUIRED)

add_executable(VOG.Common.Unit)
set_target_properties(VOG.Common.Unit PROPERTIES FOLDER "VOG/Tests")

target_include_directories(VOG.Common.Unit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(VOG.Common.Unit
    PRIVATE
        src/JSONContainerTest.cpp
)

target_link_libraries(VOG.Common.Unit
    PUBLIC
        ${PROJECT_NAME}::Common
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(VOG.Common.Unit)
