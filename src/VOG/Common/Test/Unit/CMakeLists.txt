cmake_minimum_required(VERSION 3.10.0)

find_package(GTest REQUIRED)

set(UnitTestSources
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JSONContainerTest.cpp
)

set(UnitTestHeaders
)

add_executable(VOG.Common.Unit ${UnitTestSources} ${UnitTestHeaders})
set_target_properties(VOG.Common.Unit PROPERTIES FOLDER "VOG/Tests")

target_link_libraries(VOG.Common.Unit PUBLIC VOG.Common GTest::gtest GTest::gtest_main)
target_include_directories(VOG.Common.Unit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})