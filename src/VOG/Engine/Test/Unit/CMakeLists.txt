find_package(GTest REQUIRED)

add_executable(VOG.Engine.Unit)
set_target_properties(VOG.Engine.Unit PROPERTIES FOLDER "VOG/Tests")

target_sources(VOG.Engine.Unit
    PRIVATE
        RendererTest.cpp
)

# The renderer compiles every shader listed in config.json when it is constructed.
target_compile_definitions(VOG.Engine.Unit
    PRIVATE
        VOG_TEST_SHADER_PATH="${PROJECT_SOURCE_DIR}/src/VOG/Graphics/resources/shaders"
)

target_link_libraries(VOG.Engine.Unit
    PUBLIC
        ${PROJECT_NAME}::Engine
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
# A broken lifecycle deadlocks instead of failing, so a test that hangs must not wait for ctest's
# default timeout.
gtest_discover_tests(VOG.Engine.Unit PROPERTIES TIMEOUT 60)
