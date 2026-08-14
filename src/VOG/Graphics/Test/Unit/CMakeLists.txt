find_package(GTest REQUIRED)

add_executable(VOG.Graphics.Unit)
set_target_properties(VOG.Graphics.Unit
    PROPERTIES 
        FOLDER "VOG/Tests"
)

target_include_directories(VOG.Graphics.Unit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_sources(VOG.Graphics.Unit
    PRIVATE
        Vulkan/ShaderTest.cpp
        Vulkan/GraphicsPipelineTest.cpp
        Vulkan/ShaderProgramTest.cpp
        Vulkan/DeviceLifetimeTest.cpp
        VulkanFixture.hpp
        TestShaderCompiler.hpp
)

target_link_libraries(VOG.Graphics.Unit
    PUBLIC
        VOG.Graphics
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(VOG.Graphics.Unit)
