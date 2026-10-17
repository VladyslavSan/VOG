find_package(GTest REQUIRED)

add_executable(VOG.Engine.Unit)
set_target_properties(VOG.Engine.Unit PROPERTIES FOLDER "VOG/Tests")

target_sources(VOG.Engine.Unit
    PRIVATE
        CameraViewTest.cpp
        FrameContextTest.cpp
        FramePacketQueueTest.cpp
        PipelineCacheTest.cpp
        RegistryTest.cpp
        RenderQueueTest.cpp
        RendererTest.cpp
        SceneTest.cpp
)

# The renderer compiles every shader listed in config.json when it is constructed.
target_compile_definitions(VOG.Engine.Unit
    PRIVATE
        VOG_TEST_SHADER_PATH="${PROJECT_SOURCE_DIR}/src/VOG/Graphics/resources/shaders"
)

# The tests reach behind the public API: the private definitions of Mesh and Material, the
# pipeline cache, the Vulkan objects behind the renderer and the draws of a RenderQueue.
target_include_directories(VOG.Engine.Unit
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src/VOG/Engine/src
)

target_link_libraries(VOG.Engine.Unit
    PUBLIC
        ${PROJECT_NAME}::Engine
        GTest::gtest
        GTest::gtest_main
    PRIVATE
        ${PROJECT_NAME}::Graphics
        spdlog::spdlog
)

include(GoogleTest)
# A broken lifecycle deadlocks instead of failing, so a test that hangs must not wait for ctest's
# default timeout.
gtest_discover_tests(VOG.Engine.Unit PROPERTIES TIMEOUT 60)
