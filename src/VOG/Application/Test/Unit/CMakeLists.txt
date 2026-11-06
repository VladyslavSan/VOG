find_package(GTest REQUIRED)

add_executable(VOG.Application.Unit)
set_target_properties(VOG.Application.Unit PROPERTIES FOLDER "VOG/Tests")

target_sources(VOG.Application.Unit
    PRIVATE
        ApplicationTest.cpp
)

# The tests that call run() build a real Application, which compiles the shaders of this directory.
target_compile_definitions(VOG.Application.Unit
    PRIVATE
        VOG_TEST_SHADER_PATH="${PROJECT_SOURCE_DIR}/src/VOG/Engine/resources/shaders"
)

# The test of a render thread that fails reaches behind the public API of the renderer, as the tests
# of Engine do: through its friend, which needs the private headers of Engine and the Vulkan types.
target_include_directories(VOG.Application.Unit
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/VOG/Engine/src
)

target_link_libraries(VOG.Application.Unit
    PUBLIC
        ${PROJECT_NAME}::Application
        GTest::gtest
        GTest::gtest_main
    PRIVATE
        ${PROJECT_NAME}::Graphics
        spdlog::spdlog
)

include(GoogleTest)
# A run() that is never told to quit hangs; the tests have a watchdog of their own, this is the
# backstop.
gtest_discover_tests(VOG.Application.Unit PROPERTIES TIMEOUT 60)
