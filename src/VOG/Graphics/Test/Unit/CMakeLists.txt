cmake_minimum_required(VERSION 3.10.0)

enable_testing()

set(UnitTestSources 
        ${CMAKE_CURRENT_SOURCE_DIR}/Resources/ShaderTest.cpp
    )

set(UnitTestHeaders
        ${CMAKE_CURRENT_SOURCE_DIR}/Api/GraphicsApiFixture.hpp
    )

add_executable(VOG.Graphics.Unit ${UnitTestSources} ${UnitTestHeaders})
set_target_properties(VOG.Graphics.Unit PROPERTIES FOLDER "VOG/Tests")

target_link_libraries(VOG.Graphics.Unit PUBLIC VOG.Graphics gtest gtest_main)
target_include_directories(VOG.Graphics.Unit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})