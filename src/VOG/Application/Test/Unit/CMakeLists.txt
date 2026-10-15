find_package(GTest REQUIRED)

add_executable(VOG.Application.Unit)
set_target_properties(VOG.Application.Unit PROPERTIES FOLDER "VOG/Tests")

target_sources(VOG.Application.Unit
    PRIVATE
        ApplicationTest.cpp
)

target_link_libraries(VOG.Application.Unit
    PUBLIC
        ${PROJECT_NAME}::Application
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(VOG.Application.Unit)
