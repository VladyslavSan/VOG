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
        Vulkan/CommandBufferRecorderTest.cpp
        Vulkan/DescriptorHeapTest.cpp
        Vulkan/DeviceLifetimeTest.cpp
        Vulkan/DeviceRequirementsTest.cpp
        Vulkan/InDestructorTest.cpp
        Vulkan/QueueFamiliesTest.cpp
        Vulkan/SwapchainTest.cpp
        Vulkan/UploadBarriersTest.cpp
        VulkanFixture.hpp
        VulkanValidationFixture.hpp
        TestShaderCompiler.hpp
)

# One translation unit that includes every public header, generated from the header set so that a
# header added later is covered. Compiling it is the test: it must not pull in VMA, which is a
# private dependency of VOG.Graphics.
get_target_property(publicHeaders VOG.Graphics HEADER_SET_public_headers)
set(publicHeaderIncludes "")
foreach(header IN LISTS publicHeaders)
    string(APPEND publicHeaderIncludes "#include \"${header}\"\n")
endforeach()
file(GENERATE
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/PublicHeadersTest.cpp
    CONTENT "// Generated from HEADER_SET_public_headers; see CMakeLists.txt.
#include <gtest/gtest.h>

${publicHeaderIncludes}
#ifdef AMD_VULKAN_MEMORY_ALLOCATOR_H
    #error \"A public VOG.Graphics header includes vk_mem_alloc.h\"
#endif
#ifdef VMA_VULKAN_VERSION
    #error \"A public VOG.Graphics header includes Vulkan/VmaConfig.hpp\"
#endif

TEST(PublicHeaders, doNotIncludeVma) {}
")
target_sources(VOG.Graphics.Unit PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/PublicHeadersTest.cpp)

target_compile_definitions(VOG.Graphics.Unit
    PRIVATE
        VOG_TEST_SHADER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../../resources/shaders"
)

target_link_libraries(VOG.Graphics.Unit
    PUBLIC
        VOG.Graphics
        GTest::gtest
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(VOG.Graphics.Unit)
